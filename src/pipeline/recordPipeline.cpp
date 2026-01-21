/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-01-12 14:40:08
 * @FilePath: /EdgeVision/src/pipeline/recordPipeline.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "recordPipeline.h"

// --------------------- 限制分辨率 ---------------------
static void validateAndClamp(int& w, int& h) {
    w = std::max(640, std::min(w, 1920));
    h = std::max(360, std::min(h, 1080));
}
    
// --------------------- 构造录像文件名 --------------------- 
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

class RecordPipeline::Impl {
public:
    enum class State { IDLE, RECORDING, PAUSED };

    Impl();
    ~Impl();

    // 设置与约束
    void setResolution(int w, int h); // 内部进行 clamp(640, 1920)
    void setSavePath(const std::string& path);

    // 控制接口
    void start();
    void pause();
    void resume();
    void stop();
private:
    // 环形队列
    static constexpr size_t RING_BUF_SIZE = 16;
    std::vector<FramePtr> frameBuffer_{RING_BUF_SIZE};
    std::atomic<size_t> writeIdx_{0};
    std::atomic<size_t> readIdx_{0};
    
    // 同步原语
    std::mutex MppMutex;
    std::condition_variable cv_;

    // 运行标志/状态
    std::atomic_bool running_{false};
    ThreadPauser pauser;
    std::thread recordThread;

    // 数据源
    CameraController::Config cfg;
    std::unique_ptr<CameraController> recorderCamera;

    // 录制器
    MppEncoderCorePtr videoRecorder;
    std::unique_ptr<StreamWriter> writer;

    bool sameResolution{false};
    std::string savePath{"/mnt/sdcard/"};

private:
    void cameraInit();
    void recordInit();

    void processFrame(FramePtr f);

    void onFrameReceived(FramePtr frame);
    void recordLoop();
};

// --------------------- 构造/析构 ---------------------
RecordPipeline::Impl::Impl() {
    cfg.buffer_count = 4;
    cfg.plane_count = 2;
    cfg.device = "/dev/video1";
    cfg.use_dmabuf = true;
    cfg.width = 1920;
    cfg.height = 1080;
    cfg.format = V4L2_PIX_FMT_NV12;

    cameraInit();
    recordInit();
}

RecordPipeline::Impl::~Impl() {
    stop();
} 

// --------------------- 控制 ---------------------
void RecordPipeline::Impl::start(){
    resume();
    if (running_.load()) return;
    running_.store(true);

    recorderCamera->setFrameCallback([this](FramePtr frame) {
        onFrameReceived(std::move(frame));
    });

    pauser.pause();
    recordThread = std::thread(&RecordPipeline::Impl::recordLoop, this);
    ThreadUtils::bindThreadToCore(recordThread, 2);  // 绑定解码线程到CPU CORE 2
    recorderCamera->setThreadAffinity(2);
}

void RecordPipeline::Impl::stop(){
    if (!running_.exchange(false)) return;
    resume();
    cv_.notify_all();

    videoRecorder->endOfthisEncode();

    if (writer) writer->stop();
    if (recordThread.joinable()) recordThread.join();
}

void RecordPipeline::Impl::pause(){
    pauser.pause();
    if (writer) writer->stop();
}

void RecordPipeline::Impl::resume(){
    if (running_) {
        std::string filename = makeTimestampFilename(savePath, ".h264");
        // 重新创建 writer
        if (nullptr == writer) writer = std::make_unique<StreamWriter>(filename);
        else                   writer.reset(new StreamWriter(filename));     
        if (nullptr == writer) {
            std::cerr << "[RecordPipeline][ERROR] Failed to create StreamWriter Object." << std::endl;
            return;
        }
        if (!recorderCamera) {
            std::cerr << "[RecordPipeline][ERROR] Camera for recorder invalid." << std::endl;
            return;
        }
        recorderCamera->start();
    }
    pauser.resume();
}

void RecordPipeline::Impl::recordInit() {
    MppEncoderContext::Config videoRecorderConfig = DefaultConfigs::defconfig_1080p_video(30);

    if (nullptr == videoRecorder) {
        videoRecorder = std::make_shared<MppEncoderCore>(videoRecorderConfig, 1);
    } else {
        videoRecorder->resetConfig(videoRecorderConfig);
    }

    if (!videoRecorder){
        std::cerr << "[RecordPipeline][ERROR] Failed to initialize Video Recorder." << std::endl;
        return;
    }
    // 验证分辨率
    if (cfg.width == videoRecorderConfig.prep_width && cfg.height == videoRecorderConfig.prep_height) {
        sameResolution = true;
    }
}

void RecordPipeline::Impl::cameraInit() {
    if (!recorderCamera) 
        recorderCamera = std::make_unique<CameraController>(cfg);
    else recorderCamera.reset( new CameraController(cfg) );
    if (!recorderCamera) {
        std::cerr << "[RecordPipeline][ERROR] Failed to initialize Camera Controller." << std::endl;
    }
}

void RecordPipeline::Impl::onFrameReceived(FramePtr frame) {
    if (!running_) return;
    
    size_t nextWrite = (writeIdx_ + 1) % RING_BUF_SIZE;
    // 全录制阻塞
    while (nextWrite == readIdx_.load(std::memory_order_acquire)) {
        if (!running_) return;
        std::this_thread::yield();
    }

    frameBuffer_[writeIdx_] = frame;
    writeIdx_.store(nextWrite, std::memory_order_release);
    cv_.notify_one();
}

void RecordPipeline::Impl::recordLoop() {
    while (running_){
        // 检查是否有数据
        if (readIdx_.load() == writeIdx_.load()) {
            std::unique_lock<std::mutex> lock(MppMutex);
            cv_.wait(lock, [&]{ return readIdx_ != writeIdx_ || !running_; });
        }
        pauser.wait_if_paused();

        if (!running_) break;

        // MPP 堵塞编码
        processFrame(std::move(frameBuffer_[readIdx_]));
        readIdx_.store((readIdx_ + 1) % RING_BUF_SIZE, std::memory_order_release);
    }
}

void RecordPipeline::Impl::processFrame(FramePtr frameSnapshot) {
    if (!frameSnapshot) return;

    // 获取待编码数据 meta
    MppEncoderCore::EncodedMeta meta;

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

    // std::cout << "dmaSrc("<<dmaSrc->width()<<"x"<<dmaSrc->height()<<")\n " <<"slotDma("<<slotDma->width()<<"x"<<slotDma->height()<<")" << std::endl;
    if (sameResolution) {
    // if (false) {
        // std::cout << "[VisionPipeline][DEBUG] Use submitFilledSlot() " << std::endl;
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
    if (writer && running_) writer->pushMeta(meta);
}

void RecordPipeline::Impl::setResolution(int w, int h) {
    validateAndClamp(w, h);
    cfg.width = static_cast<uint32_t>(w);
    cfg.height = static_cast<uint32_t>(h);

    pause();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    cameraInit();
    resume();
}

void RecordPipeline::Impl::setSavePath(const std::string& path) {
    savePath = path;
}

// --------------------- 外层调用 ---------------------
RecordPipeline::RecordPipeline() 
    : impl_(std::make_unique<Impl>()) {}
RecordPipeline::~RecordPipeline() = default;
void RecordPipeline::setResolution(int w, int h) {
    impl_->setResolution(w, h);
}
void RecordPipeline::setSavePath(const std::string& path) {
    impl_->setSavePath(path);
}
void RecordPipeline::start() { impl_->start(); }
void RecordPipeline::pause() { impl_->pause(); }
void RecordPipeline::resume() { impl_->resume(); }
void RecordPipeline::stop() { impl_->stop(); }
