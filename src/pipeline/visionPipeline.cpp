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

#include "v4l2param/paramControl.h"

#include "visionPipeline.h"
#include "threadUtils.h"
#include "threadPauser.h"

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
    void registerOnRGA(VisionPipeline::RGACallBack&& cb_);
    void registerOnFrameReady(VisionPipeline::ShowCallBack&& scb_);

    void setMirrorMode(bool horizontal, bool vertical);
    void setExposurePercentage(float percentage);

    bool getCurrentRawFrame(FramePtr& frame);
    bool getCurrentRGAFrame(FramePtr& frame);
    float getFPS() { return perf.getFPS(); }
    int getCameraFd();
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
    
    // 参数控制
    std::unique_ptr<ParamControl> v4l2Controller;
    ParamControl::ControlInfos currentControls_;
    int exposureIdInControls{-1};

    // 线程控制
    std::thread thread, recordThread;
    std::atomic<bool> running{false};
    std::atomic<bool> paused{false};

    ThreadPauser record_pauser;

    // 锁
    std::mutex MppMutex;
    std::mutex WriterMutex;
    std::mutex CameraMutex;
    std::mutex RGAMutex;

    // 数据缓存
    std::array<FramePtr, 2> frameBuffer;
    std::atomic<int> readIndex{0};
    std::atomic<int> writeIndex{1};
    
    // 事件驱动循环
    std::mutex loopMutex;
    std::condition_variable loopCv;
    
    // 状态
    std::atomic<bool>         sameResolution{false};
    std::atomic<RecordStatus> recordStatus{RecordStatus::Stop};
    std::atomic<ModelStatus>  modelStatus {ModelStatus::Stop};

    // 回调
    VisionPipeline::RGACallBack cb{nullptr};
    VisionPipeline::ShowCallBack showCb{nullptr};
private:
    void mainLoop();
    void record();
    
    void init();
    void v4l2CameraInit();
    void v4l2ControllerInit();
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
    ThreadUtils::bindThreadToCore(thread, 1);  // 绑定显示线程到CPU CORE 0
    ThreadUtils::setRealtimeThread(thread.native_handle(), 81); // 设置高优先级
    recordThread = std::thread([this]{
        record_pauser.pause();
        while (running){
            record_pauser.wait_if_paused();
            if (!running) break;
            if (RecordStatus::Stop == recordStatus) {
                // 析构时关闭 writer
                if (writer) writer.reset(nullptr);
                std::cout << "[VisionPipeline][DEBUG] RecordStatus -> Stop" << std::endl;
                continue;
            }
            record();
        }
    });
    ThreadUtils::bindThreadToCore(recordThread, 2);  // 绑定解码线程到CPU CORE 0
}

void VisionPipeline::Impl::stop(){
    if (!running.exchange(false)) return;
    resume();

    videoRecorder->endOfthisEncode();
    rgaProcessor->stop();
    camera->stop();

    record_pauser.resume();
    if (recordThread.joinable()) recordThread.join();
    if (writer) writer->stop();

    if (thread.joinable()) thread.join();
}

void VisionPipeline::Impl::pause(){
    if (paused.exchange(true)) return;
}

void VisionPipeline::Impl::resume(){
    if (!paused.exchange(false)) return;
}

// ------------------- 构造/析构 -------------------
VisionPipeline::Impl::Impl(const CameraController::Config& cfg)
    : cameraConfig(cfg)
{
    rawFrameQueue = std::make_shared<FrameQueue>(10);
    rgaFrameQueue = std::make_shared<FrameQueue>(10);
    // 初始化双缓冲
    frameBuffer[0] = nullptr;
    frameBuffer[1] = nullptr;
    init();
}
VisionPipeline::Impl::~Impl() { stop(); }

void VisionPipeline::Impl::init(){
    v4l2CameraInit();
    v4l2ControllerInit();
    rgaProcessorInit();
    MppEncoderInit();
}

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
        if (!rawFrameQueue->enqueue(f)) return;
        {
            std::lock_guard<std::mutex> lock(loopMutex);
        }
        loopCv.notify_one();
    
        if (ModelStatus::Start == modelStatus){
            rgaFrameQueue->enqueue(f);
        }
    });
}

// ------------------- V4L2参数控制器初始化 -------------------  
void VisionPipeline::Impl::v4l2ControllerInit(){
    int cameraFd = camera->getDeviceFd();
    if (nullptr == v4l2Controller) {    // 初始化
        v4l2Controller = std::make_unique<ParamControl>(cameraFd);
    } else {    // 重置
        v4l2Controller.reset( new ParamControl(cameraFd) );
    }
    currentControls_ = v4l2Controller->queryAllControls();
    size_t size = currentControls_.size();
    for(size_t index=0; index<size; ++index){
        auto& controlInfo = currentControls_[index];
        if (controlInfo.id != V4L2_CID_EXPOSURE) continue;
        exposureIdInControls = index;
        break;
    }
}

// ------------------- RGA处理单元初始化 -------------------  
void VisionPipeline::Impl::rgaProcessorInit(){
    rgaConfig = RgaProcessor::Config {
        .cctr = camera,
        .rawQueue = rgaFrameQueue,
        .width = cameraConfig.width,
        .height = cameraConfig.height,
        .usingDMABUF = cameraConfig.use_dmabuf,
        .dstFormat = RK_FORMAT_RGB_888,
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
    // 使用 acquire 语义读取索引
    int rIdx = readIndex.load(std::memory_order_acquire);
    
    // 获取帧的共享指针副本(增加引用计数)
    FramePtr frameSnapshot = frameBuffer[rIdx];

    // 使用缓存帧
    if (!frameSnapshot) {
        // std::cerr << "[VisionPipeline][ERROR] Current Frame is empty." << std::endl;
        // std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return nullptr;
    }
    const auto& frameSnapshotState__ =  frameSnapshot->sharedState(0);
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
    std::cout << "DRM show thread TID: " << syscall(SYS_gettid) << "\n";

    while (running.load()) {
        {
            // 使用条件变量避免 暂停/队列为空时 忙等待
            std::unique_lock<std::mutex> lock(loopMutex);
            loopCv.wait(lock, [this]() { // false 等待, true 退出等待
                // 线程退出时退出等待
                if (!running.load(std::memory_order_acquire)) {
                    return true;
                }
                // 线程暂停时等待
                if (true == paused) {
                    return false;
                }
                // 队列长度小于等于0等待
                return rawFrameQueue->size_approx() > 0;
            });
            if (!running.load(std::memory_order_acquire)) break;            
        }

        // === 双缓冲写入 ===
        // 获取当前写缓冲区索引
        int wIdx = writeIndex.load(std::memory_order_relaxed);
        // 写入新帧
        auto& rawFrame = frameBuffer[wIdx];
        if (false == rawFrameQueue->try_dequeue(rawFrame) || !rawFrame){
            continue;
        }
        
        // 内存屏障: 确保帧数据写入完成
        std::atomic_thread_fence(std::memory_order_release);
        
        // 交换读写索引
        int rIdx = readIndex.load(std::memory_order_relaxed);
        readIndex.store(wIdx, std::memory_order_release);
        writeIndex.store(rIdx, std::memory_order_relaxed);
        if (showCb) showCb(safetyGetCurrentFrame());
        // --- 计算平均帧率 ---
        perf.endFrame();
        // RGA图像传递
        if (cb && ModelStatus::Start == modelStatus){
            FramePtr frame{nullptr};
            if (getCurrentRGAFrame(frame) && frame){
                cb(frame->sharedState(0)->dmabuf_ptr, frame);
            }
        }
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
    if (status == recordStatus.exchange(status)) return false;
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
        record_pauser.resume();
    } else {
        record_pauser.pause();
    }
    return true;
}

// ------------------- 录像功能实现 -------------------  
void VisionPipeline::Impl::record() {
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
        std::cout << "[VisionPipeline][DEBUG] Use submitFilledSlot() " << std::endl;
        meta = videoRecorder->submitFilledSlotWithExternal(slot_id, dmaSrc, frameSnapshot);
    } else {
        // std::cout << "[VisionPipeline][DEBUG] Use submitFilledSlotWithExternal() " << std::endl;
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
    if (writer && running) writer->pushMeta(meta);
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

// ------------------- 获取当前V4L2节点Fd -------------------  
int VisionPipeline::Impl::getCameraFd() {
    if(camera) return camera->getDeviceFd();
    return -1;
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

// ------------------- 注册回调函数 ------------------- 
void VisionPipeline::Impl::registerOnRGA(VisionPipeline::RGACallBack&& cb_) {
    cb = std::move(cb_);
}
void VisionPipeline::Impl::registerOnFrameReady(VisionPipeline::ShowCallBack&& scb_) {
    showCb = std::move(scb_);
}
// ------------------- 镜像 / 曝光 ------------------- 
void VisionPipeline::Impl::setMirrorMode(bool horizontal, bool vertical) {
    // 水平镜像
    v4l2Controller->setControl(V4L2_CID_HFLIP, horizontal ? 1 : 0);
    // 垂直镜像
    v4l2Controller->setControl(V4L2_CID_VFLIP, vertical ? 1 : 0);
}

void VisionPipeline::Impl::setExposurePercentage(float percentage) {
    if (currentControls_.empty()) return;
    auto& info = currentControls_.at(exposureIdInControls);
    auto range = info.max - info.min;
    auto currentValue = info.min + range * percentage / 100;
    v4l2Controller->setControl(info.id, currentValue);
}

// ------------------- 重新配置 ------------------- 
void VisionPipeline::Impl::resetConfig(const CameraController::Config& newConfig) {
    static auto queueClear = [this] (const std::shared_ptr<FrameQueue>& queue) {
        while (queue->size_approx() > 0) {
            FramePtr tmp;
            queue->try_dequeue(tmp);
        }
    };
    // 更新配置
    cameraConfig = newConfig;
    // 暂停线程
    pause();
    tryRecord(VisionPipeline::RecordStatus::Stop);
    setModelRunningStatus(VisionPipeline::ModelStatus::Stop);
    std::this_thread::sleep_for(1ms);
    
    // 重置状态
    sameResolution.store(false);
    recordStatus.store(RecordStatus::Stop);
    modelStatus.store(ModelStatus::Stop);

    // 析构
    frameBuffer[0].reset();
    frameBuffer[1].reset();
    rgaProcessor.reset();
    camera.reset();
    
    queueClear(rawFrameQueue);
    queueClear(rgaFrameQueue);

    init();
    // 唤醒线程
    camera->start();
    resume();
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
void VisionPipeline::setMirrorMode(bool horizontal, bool vertical) {
    impl_->setMirrorMode(horizontal, vertical);
}
void VisionPipeline::setExposurePercentage(float percentage) {
    impl_->setExposurePercentage(percentage);
}
bool VisionPipeline::tryCapture() { return impl_->tryCapture(); }
bool VisionPipeline::tryRecord(RecordStatus stauts) { return impl_->tryRecord(stauts); }
bool VisionPipeline::setModelRunningStatus(ModelStatus stauts) { return impl_->setModelRunningStatus(stauts); }
void VisionPipeline::registerOnRGA(RGACallBack cb_) { impl_->registerOnRGA(std::move(cb_)); }
void VisionPipeline::registerOnFrameReady(VisionPipeline::ShowCallBack&& scb_) {
    impl_->registerOnFrameReady(std::move(scb_));
}
void VisionPipeline::resetConfig(const CameraController::Config& newConfig){ impl_->resetConfig(newConfig); }
bool VisionPipeline::getCurrentRawFrame(FramePtr& frame) { return impl_->getCurrentRawFrame(frame); }
bool VisionPipeline::getCurrentRGAFrame(FramePtr& frame) { return impl_->getCurrentRGAFrame(frame); }
float VisionPipeline::getFPS() { return impl_->getFPS(); }
int VisionPipeline::getCameraFd() { return impl_->getCameraFd(); }