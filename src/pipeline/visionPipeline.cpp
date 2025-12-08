/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-05 21:58:03
 * @FilePath: /EdgeVision/src/pipeline/visionPipeline.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include <chrono>
#include <iomanip>
#include <sstream>
#include <sys/syscall.h>
#include <sys/stat.h> // POSIX 文件操作

#include "visionPipeline.h"
#include "threadUtils.h"

using namespace std::chrono_literals;
// --------------- 帧数分析 --------------- 
class FpsPref {
public:
    FpsPref()
        : fps_(0.0f),
            frameCount_(0),
            lastTime_(std::chrono::steady_clock::now())
    {}

    // 每处理完一帧调用一次
    inline void endFrame() {
        ++frameCount_;

        auto now = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime_).count();

        // 每 500ms 更新一次 FPS, 减少抖动
        if (500 <= diff) {
            float curFps = static_cast<float>(frameCount_) * 1000.0f / static_cast<float>(diff);
            fps_.store(curFps, std::memory_order_relaxed);

            // 复位窗口
            frameCount_ = 0;
            lastTime_ = now;
        }
    }

    // UI / 监控线程调用
    inline float getFPS() const {
        return fps_.load(std::memory_order_relaxed);
    }

    inline void reset() {
        fps_.store(0.0f, std::memory_order_relaxed);
        frameCount_ = 0;
        lastTime_ = std::chrono::steady_clock::now();
    }

private:
    std::atomic<float> fps_;   // 当前 FPS
    uint32_t frameCount_;      // 窗口内帧数
    std::chrono::steady_clock::time_point lastTime_;
};

// --------------- 构造录像文件名 --------------- 
static std::string makeTimestampFilename(const std::string& dir, const std::string& suffix) {
    using namespace std::chrono;

    auto now = system_clock::now();
    auto t = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_time;
    localtime_r(&t, &tm_time);

    std::ostringstream oss;
    if (false == dir.empty()) {
        mkdir(dir.c_str(), 0755);
        oss << dir;
        if ('/' != dir.back()) oss << "/";
    }

    oss << std::put_time(&tm_time, "%Y%m%d_%H%M%S")
        << "_" << std::setw(3) << std::setfill('0') << ms.count()
        << suffix;

    return oss.str();
}

// --------------- 流水线Impl --------------- 
class VisionPipeline::Impl {
public:
    Impl(const CameraController::Config& cameraConfig);
    ~Impl();

    void start();
    void stop();
    void pause();
    void resume();

    bool tryCapture();
    bool tryRecord(RecordStatus status);
    bool setModelRunningStatus(ModelStatus status);

    bool getCurrentRawFrame(FramePtr& frame);
    bool getCurrentRGAFrame(FramePtr& frame);
    float getFPS() { return perf.getFPS(); }

    // 热插拔重建
    void resetConfig(const CameraController::Config& newConfig);
private:
    // 原始帧队列
    std::shared_ptr<FrameQueue> rawFrameQueue;
    // RGA处理线程
    std::shared_ptr<FrameQueue> rgaFrameQueue;

    // 摄像头
    CameraController::Config cameraConfig;
    std::shared_ptr<CameraController> camera;
    FpsPref perf;
    
    // RGA
    RgaProcessor::Config rgaConfig;
    std::unique_ptr<RgaProcessor> rgaProcessor;

    // MPP
    MppEncoderCorePtr videoRecorder;
    std::unique_ptr<JpegEncoder> pictureCapturer;
    std::unique_ptr<StreamWriter> writer;
    // 线程控制
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> refreshing{false};

    // 锁
    std::mutex MppMutex;
    std::mutex WriterMutex;
    std::mutex CameraMutex;
    std::mutex RGAMutex;

    // 数据缓存
    std::mutex latestFrameMutex;
    FramePtr latestFrame = nullptr;
    
    // 状态
    std::atomic<bool>         sameResolution{false};
    std::atomic<RecordStatus> recordStatus{RecordStatus::Stop};
    std::atomic<ModelStatus>  modelStatus {ModelStatus::Stop};

private:
    void mainLoop();
    void record();
    
    void v4l2CameraInit();
    void rgaProcessorInit();
    void MppEncoderInit();

    FramePtr safetyGetCurrentFrame();
};

// ------------------- 开启/关闭/暂停/唤醒 -------------------  
void VisionPipeline::Impl::start(){
    resume();
    if (running.load()) return;
    running.store(true);

    camera->start();
    camera->setThreadAffinity(2);

    thread = std::thread(&VisionPipeline::Impl::mainLoop, this);
}
void VisionPipeline::Impl::stop(){
    running.store(false);
    resume(); refreshing.store(false);
    videoRecorder->endOfthisEncode();
    rgaProcessor->stop();
    camera->stop();
    if (thread.joinable()) thread.join();
}
void VisionPipeline::Impl::pause(){
    if (paused.load()) return;
    paused.store(true);
}
void VisionPipeline::Impl::resume(){
    if (false == paused.load()) return;
    paused.store(false);
}

// ------------------- 构造/析构 -------------------
VisionPipeline::Impl::Impl(const CameraController::Config& cfg)
    : cameraConfig(cfg)
{
    rawFrameQueue = std::make_shared<FrameQueue>(10);
    rgaFrameQueue = std::make_shared<FrameQueue>(10);
    v4l2CameraInit();
    rgaProcessorInit();
    MppEncoderInit();
}
VisionPipeline::Impl::~Impl() { stop(); }

// ------------------- V4L2摄像头相关初始化 -------------------  
void VisionPipeline::Impl::v4l2CameraInit() {
    std::lock_guard<std::mutex> lk(CameraMutex);

    if (nullptr == camera) {    // 初始化
        camera = std::make_shared<CameraController>(cameraConfig);
    } else {    // 重置
        camera.reset( new CameraController(cameraConfig) );
    }
    
    if (!camera) {
        std::cerr << "[VisionPipeline][ERROR] Failed to create CameraController object." << std::endl;
        return;
    }
    // 设置回调入队队列
    camera->setFrameCallback([this](FramePtr f) {
        rawFrameQueue->enqueue(f);
        if (ModelStatus::Start == modelStatus){
            rgaFrameQueue->enqueue(f);
        }
    });
}

// ------------------- RGA处理单元初始化 -------------------  
void VisionPipeline::Impl::rgaProcessorInit(){
    rgaConfig = RgaProcessor::Config {
        .cctr = camera,
        .rawQueue = rgaFrameQueue,
        .width = cameraConfig.width,
        .height = cameraConfig.height,
        .usingDMABUF = cameraConfig.use_dmabuf,
        .dstFormat = RK_FORMAT_RGBA_8888,
        .srcFormat = convertV4L2toRGAFormat(cameraConfig.format),
        .poolSize = 5
    };

    std::lock_guard<std::mutex> lk(RGAMutex);
    if (nullptr == rgaProcessor) rgaProcessor = std::make_unique<RgaProcessor>(rgaConfig);
    else rgaProcessor.reset( new RgaProcessor(rgaConfig) );

    if (!rgaProcessor) { 
        std::cerr << "[VisionPipeline][ERROR] Failed to create RgaProcessor object." << std::endl;
        return;
    }
}

// ------------------- 获取可用FramePtr -------------------  
FramePtr VisionPipeline::Impl::safetyGetCurrentFrame(){
    FramePtr frameSnapshot;
    {
        std::lock_guard<std::mutex> lk(latestFrameMutex);
        frameSnapshot = latestFrame;
    }
    // 使用缓存帧
    if (!frameSnapshot) {
        std::cerr << "[VisionPipeline][ERROR] Current Frame is empty." << std::endl;
        return nullptr;
    }
    const auto frameSnapshotState__ =  frameSnapshot->sharedState(0);
    if (!frameSnapshotState__ || !frameSnapshotState__->valid) {
        std::cerr << "[VisionPipeline][ERROR] Current Frame is invalid" << std::endl;
        return nullptr;
    }
    return frameSnapshot;
}
// ------------------- Mpp编码相关初始化(录像/拍照) -------------------  
void VisionPipeline::Impl::MppEncoderInit() {
    auto cameraW = cameraConfig.width;
    auto cameraH = cameraConfig.height;
    MppEncoderContext::Config videoRecorderConfig = DefaultConfigs::defconfig_1080p_video(30);
    if (cameraW == videoRecorderConfig.prep_width && cameraH == videoRecorderConfig.prep_height)
        sameResolution.store(true); // 系统分辨率标志

    JpegEncoder::Config pictureCapturerConfig {
        .width = cameraW,
        .height = cameraH,
        .format = MPP_FMT_YUV420SP,
        .quality = 8,
        .save_dir = "/tmp/photos"
    };
    std::lock_guard<std::mutex> lk(MppMutex);
    if (nullptr == videoRecorder || nullptr == pictureCapturer) { // 第一次调用需要使用 make_
        videoRecorder = std::make_shared<MppEncoderCore>(videoRecorderConfig, 1);
        pictureCapturer = std::make_unique<JpegEncoder>(pictureCapturerConfig);
    } else {    // 重置配置
        videoRecorder->resetConfig(videoRecorderConfig);
        pictureCapturer->resetConfig(pictureCapturerConfig);
    }

    if (!videoRecorder || !pictureCapturer) {
        std::cerr << "[VisionPipeline][ERROR] Failed to initialize Mpp Encoder." << std::endl;
        return;
    }
}

// ------------------- 主循环 -------------------  
void VisionPipeline::Impl::mainLoop() {
    ThreadUtils::bindCurrentThreadToCore(0);  // 绑定显示线程到CPU CORE 0
    std::cout << "DRM show thread TID: " << syscall(SYS_gettid) << "\n";
    
    auto beforeTime = std::chrono::steady_clock::now();
    while (running.load()) {
        // 线程状态
        if (refreshing || paused) {
            std::this_thread::yield();
            if (false == running) break;
            continue;
        }
        // 获取帧数据
        FramePtr rawFrame;
        if (false == rawFrameQueue->try_dequeue(rawFrame)) continue;
        if (!rawFrame) continue;
        {
            // 缓存一帧
            std::lock_guard<std::mutex> lk(latestFrameMutex);
            latestFrame = rawFrame;
        }        
        // --- 计算平均帧率 ---
        perf.endFrame();
        record();
    }
}

// ------------------- 拍照实现 -------------------  
bool VisionPipeline::Impl::tryCapture(){
    // 获取可用frame
    auto frame = safetyGetCurrentFrame();
    if (!frame) return false;
    // 拍照
    return pictureCapturer->captureFromDmabuf(frame->sharedState(0)->dmabuf_ptr);
}

// ------------------- 更新录像状态标志 -------------------  
bool VisionPipeline::Impl::tryRecord(RecordStatus status) {
    recordStatus.store(status);
    // std::lock_guard<std::mutex> lk(WriterMutex);
    if (RecordStatus::Start == status) {
        // 生成带时间戳的文件名
        std::string filename = makeTimestampFilename("/tmp/videos", ".h264");
        // 重新创建 writer
        if (nullptr == writer) writer = std::make_unique<StreamWriter>(filename);
        else                   writer.reset(new StreamWriter(filename));     
        if (nullptr == writer) {
            std::cerr << "[VisionPipeline][ERROR] Failed to create StreamWriter Object." << std::endl;
            return false;
        }
        // std::cout << "[VisionPipeline][DEBUG] Create StreamWriter Object." << std::endl;
    }
    return true;
}

// ------------------- 录像功能实现 -------------------  
void VisionPipeline::Impl::record() {
    // 根据录像状态调用
    if (RecordStatus::Stop == recordStatus) {
        // 析构时关闭 writer
        if (writer) writer.reset(nullptr);
        return;
    }
    // 获取可用frame
    FramePtr frameSnapshot = safetyGetCurrentFrame();
    if (!frameSnapshot) return;
    
    // 获取待编码数据 meta
    MppEncoderCore::EncodedMeta meta;
    std::lock_guard<std::mutex> lk(MppMutex);
    // 获取可用slot
    auto _ = videoRecorder->acquireWritableSlot();
    auto slotDma = _.first;
    auto slot_id = _.second;

    if (!slotDma || slot_id < 0){
        return;
    }
    // RAII自动releaseSlot
    SlotGuard guard(videoRecorder.get(), slot_id);
    // 获取原始NV12数据
    auto dmaSrc = frameSnapshot->sharedState(0)->dmabuf_ptr;
    if (!dmaSrc) return;

// TODO: 修复使用现成DMABUF时死机问题
    if (false ){ //&& sameResolution.load()) {
        std::cout << "[VisionPipeline][DEBUG] Use submitFilledSlotWithExternal() " << std::endl;
        meta = videoRecorder->submitFilledSlotWithExternal(slot_id, dmaSrc, frameSnapshot);
    } else {
        rga_buffer_t src = wrapbuffer_fd(dmaSrc->fd(), dmaSrc->width(), 
                                    dmaSrc->height(), convertDRMtoRGAFormat(dmaSrc->format()));
        src.wstride = dmaSrc->pitch();
        src.hstride = dmaSrc->height();
        im_rect srcR{0, 0, static_cast<int>(dmaSrc->width()), static_cast<int>(dmaSrc->height())};

        rga_buffer_t dst = wrapbuffer_fd(slotDma->fd(), slotDma->width(), 
                                        slotDma->height(), convertDRMtoRGAFormat(slotDma->format()));
        dst.wstride = slotDma->pitch();
        dst.hstride = slotDma->height();
        im_rect dstR{0, 0, static_cast<int>(slotDma->pitch()), static_cast<int>(slotDma->height())};
        
        RgaConverter::RgaParams param{
            .src = src,
            .src_rect = srcR,
            .dst = dst,
            .dst_rect = dstR
        };
        // 拷贝数据到 slot 内部 DMABUF
        if(RgaConverter::instance().ImageResize(param) != IM_STATUS_SUCCESS){
            std::cerr << "[VisionPipeline][ERROR] RGA-copy failed.\n";
            return;
        }
        meta = videoRecorder->submitFilledSlot(slot_id);
    }

    if (meta.core_id == -1 || meta.slot_id != slot_id) {
        std::cerr << "[VisionPipeline][ERROR] Get invalid mete of MppEncoderCore" << std::endl;
        return;
    }
    // 释放RAII的管理避免提前释放
    guard.release();
    // 提交给写线程
    if (writer) writer->pushMeta(meta);
}

// ------------------- 获取当前可用帧 -------------------  
bool VisionPipeline::Impl::getCurrentRawFrame(FramePtr& frame) {
    // 检测缓存帧有效性
    frame = safetyGetCurrentFrame(); // 获取失败返回nullptr
    if (!frame) return false;
    return true;
}

// ------------------- 获取RGA转换后数据 -------------------  
bool VisionPipeline::Impl::getCurrentRGAFrame(FramePtr& frame) {
    auto modelstatus__ = modelStatus.load(std::memory_order_relaxed);
    if (ModelStatus::Stop == modelstatus__) return false;
    std::lock_guard<std::mutex> lk(RGAMutex);
    if (rgaProcessor->dump(frame, 33) < 0) return false;
    if (!frame || !frame->sharedState(0)->valid) return false;
    return true;
}

// ------------------- 开启RGA图像格式转换 ------------------- 
bool VisionPipeline::Impl::setModelRunningStatus(ModelStatus status) {
    // 清空缓存帧
    FramePtr oldFrame;
    while(rgaFrameQueue->try_dequeue(oldFrame));
    // 加锁
    {
        std::lock_guard<std::mutex> lk(RGAMutex);
        if (!rgaProcessor) return false;
        // 修改状态
        if (ModelStatus::Start == status){
            rgaProcessor->start();
        } else if (ModelStatus::Stop == status) {
            rgaProcessor->pause();
        }
    }
    modelStatus.store(status);
    return true;
}

// ------------------- 重新配置 ------------------- 
void VisionPipeline::Impl::resetConfig(const CameraController::Config& newConfig) {
    // 暂停线程
    refreshing.store(true);
    tryRecord(VisionPipeline::RecordStatus::Stop);
    setModelRunningStatus(VisionPipeline::ModelStatus::Stop);
    std::this_thread::sleep_for(10ms);

    // 析构
    latestFrame.reset();
    rgaProcessor.reset();
    camera.reset();

    // 重置状态
    sameResolution.store(false);
    recordStatus.store(RecordStatus::Stop);
    modelStatus.store(ModelStatus::Stop);
    v4l2CameraInit();
    rgaProcessorInit();
    MppEncoderInit();
    // 唤醒线程
    camera->start();
    refreshing.store(false);
}

// -------------- 外部接口 --------------
VisionPipeline::VisionPipeline(const CameraController::Config& cameraConfig) {
    impl_ = std::make_unique<Impl>(cameraConfig);
    if (!impl_ || !impl_.get()) {
        std::cerr << "[VisionPipeline][ERROR] Init failed." << std::endl;
        return;
    } else {
        std::cout << "[VisionPipeline] Init successed." << std::endl;
    }
}
VisionPipeline::~VisionPipeline() = default;

void VisionPipeline::start() { impl_->start(); }
void VisionPipeline::stop()  { impl_->stop(); }
void VisionPipeline::pause() { impl_->pause(); }
void VisionPipeline::resume() { impl_->resume(); }
bool VisionPipeline::tryCapture() { return impl_->tryCapture(); }
bool VisionPipeline::tryRecord(RecordStatus stauts) { return impl_->tryRecord(stauts); }
bool VisionPipeline::setModelRunningStatus(ModelStatus stauts) { return impl_->setModelRunningStatus(stauts); }
void VisionPipeline::resetConfig(const CameraController::Config& newConfig){ impl_->resetConfig(newConfig); }
bool VisionPipeline::getCurrentRawFrame(FramePtr& frame) { return impl_->getCurrentRawFrame(frame); }
bool VisionPipeline::getCurrentRGAFrame(FramePtr& frame) { return impl_->getCurrentRGAFrame(frame); }
float VisionPipeline::getFPS() { return impl_->getFPS(); }