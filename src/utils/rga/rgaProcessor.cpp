/*
 * @FilePath: /utilsCore/src/utils/rga/rgaProcessor.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-17 22:51:38
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "rga/rgaProcessor.h"
#include "logger_v2.h"  // 新的日志系统
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include "threadUtils.h"

using namespace utils::rga;

// ============================================================================
// RgbaBuffer 实现
// ============================================================================

RgbaBuffer::RgbaBuffer(std::shared_ptr<DmaBuffer> dmaBuf) 
    : state_(std::make_shared<SharedBufferState>(dmaBuf, nullptr))
    , memory_type_(Frame::MemoryType::DMABUF) {
    if (!dmaBuf || dmaBuf->fd() < 0) {
        throw std::invalid_argument("Invalid DMA-BUF provided");
    }
}

RgbaBuffer::RgbaBuffer(void* data, size_t size)
    : memory_type_(Frame::MemoryType::MMAP) {
    if (!data || size == 0) {
        throw std::invalid_argument("Invalid MMAP parameters");
    }
    state_ = std::make_shared<SharedBufferState>(-1, data, size);
}

RgbaBuffer::~RgbaBuffer() {
    if (state_) {
        state_->valid = false;
        state_.reset();
    }
}

RgbaBuffer::RgbaBuffer(RgbaBuffer&& other) noexcept
    : state_(std::move(other.state_))
    , in_use_(other.in_use_.load(std::memory_order_relaxed))
    , memory_type_(other.memory_type_) {
    other.in_use_.store(false, std::memory_order_relaxed);
}

RgbaBuffer& RgbaBuffer::operator=(RgbaBuffer&& other) noexcept {
    if (this != &other) {
        // 清理当前资源
        if (state_) {
            state_->valid = false;
            state_.reset();
        }
        
        // 转移资源
        state_ = std::move(other.state_);
        in_use_.store(other.in_use_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        other.in_use_.store(false, std::memory_order_relaxed);

        memory_type_ = other.memory_type_;
    }
    return *this;
}

bool RgbaBuffer::isValid() const noexcept {
    if (!state_ || !state_->valid) {
        return false;
    }
    
    if (memory_type_ == Frame::MemoryType::DMABUF) {
        return state_->dmabuf_ptr && state_->dmabuf_ptr->fd() >= 0;
    } else {
        return state_->start != nullptr && state_->length > 0;
    }
}

int RgbaBuffer::getFd() const noexcept {
    if (memory_type_ != Frame::MemoryType::DMABUF || !state_ || !state_->dmabuf_ptr) {
        return -1;
    }
    return state_->dmabuf_ptr->fd();
}

void* RgbaBuffer::getVirtualAddress() const noexcept {
    if (memory_type_ != Frame::MemoryType::MMAP || !state_) {
        return nullptr;
    }
    return state_->start;
}

size_t RgbaBuffer::getSize() const noexcept {
    return state_ ? state_->length : 0;
}

// ============================================================================
// RgaProcessorConfig 实现
// ============================================================================

bool RgaProcessorConfig::isValid() const noexcept {
    if (width == 0 || height == 0) {
        return false;
    }
    
    if (poolSize <= 0 || poolSize > 100) { // 合理的缓冲池大小限制
        return false;
    }
    
    if (maxPendingTasks <= 0 || maxPendingTasks > 1000) {
        return false;
    }
    // 验证有效性
    if (!utils::rga::isValidRgaFormat(srcFormat) || !utils::rga::isValidRgaFormat(dstFormat)) {
        return false;
    }
    
    return true;
}

std::string RgaProcessorConfig::toString() const {
    char buffer[512];
    snprintf(buffer, sizeof(buffer),
             "RgaProcessorConfig{width=%u, height=%u, srcFormat=%d, dstFormat=%d, "
             "usingDMABUF=%s, poolSize=%d, maxPendingTasks=%d}",
             width, height, srcFormat, dstFormat,
             usingDMABUF ? "true" : "false",
             poolSize, maxPendingTasks);
    return std::string(buffer);
}

// ============================================================================
// RgaProcessor 实现
// ============================================================================

RgaProcessor::RgaProcessor(const RgaProcessorConfig& config)
    : config_(config)
    , frameType_(config.usingDMABUF ? Frame::MemoryType::DMABUF : Frame::MemoryType::MMAP) {
    
    // 验证配置
    if (!config_.isValid()) {
        throw std::invalid_argument("Invalid RgaProcessor configuration: " + config_.toString());
    }
    
    // 初始化缓冲池
    try {
        initBufferPool();
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to initialize buffer pool: ") + e.what());
    }
    
    // 创建线程
    threadPool_ = std::make_unique<asyncThreadPool>(config_.poolSize);
    if (!config_.rawQueue.lock()) {
        LOG_ERROR("Raw frame queue invalid.\n");
    }
    rawQueue_ = config_.rawQueue;
    LOG_INFO("RgaProcessor initialized with config: %s", config_.toString().c_str());
}

RgaProcessor::~RgaProcessor() {
    try {
        stop();
        cleanupPendingTasks();
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in destructor: %s", e.what());
    }
}

void RgaProcessor::initBufferPool() {
    std::lock_guard<std::mutex> lock(bufferPoolMutex_);
    
    bufferPool_.clear();
    bufferPool_.reserve(config_.poolSize);
    
    for (int i = 0; i < config_.poolSize; ++i) {
        try {
            if (frameType_ == Frame::MemoryType::MMAP) {
                size_t bufferSize = config_.width * config_.height * 4; // RGBA8888
                void* data = mmap(nullptr, bufferSize,
                                 PROT_READ | PROT_WRITE,
                                 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
                if (data == MAP_FAILED) {
                    throw std::runtime_error("mmap failed: " + std::string(strerror(errno)));
                }
                
                bufferPool_.emplace_back(data, bufferSize);
                // Debug: Created MMAP buffer %d: addr=%p, size=%zu
                // LOG_ERROR("[RgaProcessor] Created MMAP buffer %d: addr=%p, size=%zu\n", i, data, bufferSize);
            } else {
                uint32_t drmFormat = convertRGAtoDrmFormat(config_.dstFormat);
                if (drmFormat == static_cast<uint32_t>(-1)) {
                    throw std::runtime_error("Invalid destination format: " + std::to_string(config_.dstFormat));
                }
                
                auto dmaBuf = DmaBuffer::create(config_.width, config_.height, drmFormat, 0, 0);
                if (!dmaBuf || dmaBuf->fd() < 0) {
                    throw std::runtime_error("DMA-BUF creation failed");
                }
                
                bufferPool_.emplace_back(dmaBuf);
                // Debug: Created DMA-BUF buffer %d: fd=%d
                // LOG_ERROR("[RgaProcessor] Created DMA-BUF buffer %d: fd=%d\n", i, dmaBuf->fd());
            }
        } catch (const std::exception& e) {
            // 清理已创建的缓冲
            bufferPool_.clear();
            throw std::runtime_error(std::string("Failed to create buffer ") + std::to_string(i) + ": " + e.what());
        }
    }
    
    LOG_INFO("Buffer pool initialized with %zu buffers", bufferPool_.size());
}

RgaProcessorError RgaProcessor::start() {
    if (running_.load()) {
        LOG_WARN("Already running");
        return RgaProcessorError::THREAD_ALREADY_RUNNING;
    }
    
    if (stopping_.load()) {
        LOG_WARN("Is stopping, cannot start");
        return RgaProcessorError::THREAD_NOT_RUNNING;
    }
    
    // 恢复暂停状
    if (pauser_.is_paused()) {
        pauser_.resume();
    }
    
    // 启动工作线程
    running_.store(true);
    stopping_.store(false);
    
    try {
        workerThread_ = std::thread(&RgaProcessor::workerThreadMain, this);
        
        // 设置线程亲和性(如果配置了)
        if (config_.threadAffinity >= 0) {
            setThreadAffinity(config_.threadAffinity);
        }
        
        LOG_INFO("Started successfully");
        return RgaProcessorError::SUCCESS;
    } catch (const std::exception& e) {
        running_.store(false);
        LOG_ERROR("Failed to start: %s", e.what());
        return RgaProcessorError::THREAD_NOT_RUNNING;
    }
}

RgaProcessorError RgaProcessor::stop() {
    if (!running_.load()) {
        LOG_WARN("Not running");
        return RgaProcessorError::THREAD_NOT_RUNNING;
    }
    
    LOG_INFO("Stopping...");
    stopping_.store(true);
    running_.store(false);
    pauser_.resume();
    
    // 通知工作线程
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        workerCV_.notify_all();
    }
    {
        std::lock_guard<std::mutex> lock(taskQueueMutex_);
        taskQueueCv_.notify_all();
    }
    
    // 等待工作线程结束
    if (workerThread_.joinable()) {
        try {
            workerThread_.join();
            LOG_INFO("RgaProcessor worker thread stopped");
        } catch (const std::exception& e) {
            LOG_ERROR("Error joining worker thread: %s", e.what());
        }
    }

    {
        std::unique_lock<std::mutex> lock(taskQueueMutex_);
        taskQueueCv_.wait_for(lock, std::chrono::milliseconds(500), [this] {
            return inFlightTasks_.load(std::memory_order_relaxed) == 0;
        });
    }

    cleanupPendingTasks();
    
    LOG_INFO("RgaProcessor stopped successfully");
    return RgaProcessorError::SUCCESS;
}

void RgaProcessor::pause() {
    pauser_.pause();
    // Debug: RgaProcessor paused
    // LOG_ERROR("[RgaProcessor] Paused\n");
}

void RgaProcessor::resume() {
    pauser_.resume();
    // Debug: RgaProcessor resumed
    // LOG_ERROR("[RgaProcessor] Resumed\n");
}

int RgaProcessor::getAvailableBufferIndex() {
    std::lock_guard<std::mutex> lock(bufferPoolMutex_);
    
    const size_t poolSize = bufferPool_.size();
    if (poolSize == 0) {
        return -1;
    }
    
    // 从当前索引开始循环查
    for (size_t i = 0; i < poolSize; ++i) {
        size_t tryIdx = (currentIndex_.load() + i) % poolSize;
        auto& buffer = bufferPool_[tryIdx];
        
        // 检查缓冲区是否可用
        if (buffer.isInUse() || !buffer.isValid()) {
            continue;
        }
        
        // 标记为使用中
        buffer.setInUse(true);
        currentIndex_.store((tryIdx + 1) % poolSize);
        
        // Trace: Allocated buffer index %zu
        // LOG_ERROR("[RgaProcessor] Allocated buffer index %zu\n", tryIdx);
        return static_cast<int>(tryIdx);
    }
    
    LOG_WARN("No available buffers in pool");
    return -1;
}

int RgaProcessor::processDmaBufFrame(rga_buffer_t& src, rga_buffer_t& dst, 
                                    std::shared_ptr<DmaBuffer> srcDmaBuf) {
    if (!srcDmaBuf || srcDmaBuf->fd() < 0) {
        LOG_ERROR("Invalid source DMA-BUF");
        return -1;
    }
    
    int index = getAvailableBufferIndex();
    if (index < 0) {
        LOG_WARN("No available buffer for DMA-BUF processing");
        return -1;
    }
    
    auto& dstBuffer = bufferPool_[index];
    if (!dstBuffer.isValid()) {
        LOG_ERROR("Destination buffer %d is invalid", index);
        dstBuffer.setInUse(false); 
        // 释放缓冲
        return -1;
    }
    
    // 设置源缓冲区参数
    src = wrapbuffer_fd(srcDmaBuf->fd(), 
                       srcDmaBuf->width(), 
                       srcDmaBuf->height(), 
                       config_.srcFormat);
    
    // 设置目标缓冲区参
    dst = wrapbuffer_fd(dstBuffer.getFd(),
                       config_.width,
                       config_.height,
                       config_.dstFormat);
    
    LOG_TRACE("DMA-BUF processing: src_fd=%d, dst_fd=%d, index=%d",
             srcDmaBuf->fd(), dstBuffer.getFd(), index);
    
    return index;
}

int RgaProcessor::processMmapFrame(rga_buffer_t& src, rga_buffer_t& dst, void* data) {
    if (!data) {
        LOG_ERROR("Invalid source data pointer");
        return -1;
    }
    
    int index = getAvailableBufferIndex();
    if (index < 0) {
        LOG_WARN("No available buffer for MMAP processing");
        return -1;
    }
    
    auto& dstBuffer = bufferPool_[index];
    if (!dstBuffer.isValid()) {
        LOG_ERROR("Destination buffer %d is invalid", index);
        dstBuffer.setInUse(false);
        // 释放缓冲
        return -1;
    }
    
    // 设置源缓冲区参数
    src.width = config_.width;
    src.height = config_.height;
    src.wstride = config_.width;
    src.hstride = config_.height;
    src.format = config_.srcFormat;
    src.vir_addr = data;
    
    // 设置目标缓冲区参
    dst = src;
    dst.vir_addr = dstBuffer.getVirtualAddress();
    dst.format = config_.dstFormat;
    
    LOG_TRACE("MMAP processing: src_addr=%p, dst_addr=%p, index=%d",
             data, dstBuffer.getVirtualAddress(), index);
    
    return index;
}

int RgaProcessor::processFrameAuto(rga_buffer_t& src, rga_buffer_t& dst, 
                                  std::weak_ptr<Frame> frame) {
    auto framePtr = frame.lock();
    if (!framePtr) {
        LOG_WARN("Frame expired");
        return -1;
    }
    
    if (!validateFrameFormat(framePtr)) {
        LOG_WARN("Invalid frame format");
        return -1;
    }
    
    switch (frameType_) {
        case Frame::MemoryType::MMAP:
            return processMmapFrame(src, dst, framePtr->data());
            
        case Frame::MemoryType::DMABUF:
            return processDmaBufFrame(src, dst, framePtr->sharedState(0)->dmabuf_ptr);
            
        default:
            LOG_ERROR("Unsupported frame type: %d", static_cast<int>(frameType_));
            return -1;
    }
}

bool RgaProcessor::validateFrameFormat(const FramePtr& frame) const {
    if (!frame) {
        return false;
    }
    
    // 检查帧尺寸是否匹配
    if (frame->meta.w != config_.width || frame->meta.h != config_.height) {
        LOG_WARN("Frame size mismatch: expected %ux%u, got %ux%u",
                   config_.width, config_.height, frame->meta.w, frame->meta.h);
        return false;
    }
    
    // 检查帧内存类型是否匹配
    if (frame->type() != frameType_) {
        LOG_WARN("Frame memory type mismatch: expected %d, got %d",
                   static_cast<int>(frameType_), static_cast<int>(frame->type()));
        return false;
    }
    
    return true;
}

FramePtr RgaProcessor::processInference() {
    rga_buffer_t src = {};
    rga_buffer_t dst = {};
    FramePtr rawFrame = nullptr;
    FramePtr dstFrame = nullptr;
    
    // 获取原始帧队
    auto queue = rawQueue_.lock();
    if (!queue) {
        LOG_WARN("Raw frame queue expired");
        return dstFrame;
    }
    
    // 尝试从队列获取帧(最多重3次)
    for (int retry = 0; retry < 3; ++retry) {
        if (queue->try_dequeue(rawFrame) && rawFrame) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    if (!rawFrame) {
        LOG_TRACE("No frame available in queue");
        return dstFrame;
    }
    
    if (!running_.load()) {
        LOG_TRACE("Processor not running, dropping frame");
        return dstFrame;
    }
    
    // 处理
    int index = processFrameAuto(src, dst, rawFrame);
    if (index < 0) {
        LOG_WARN("Failed to process frame, dropping");
        return dstFrame;
    }
    
    // 获取帧状
    auto state = rawFrame->sharedState(0);
    if (!state || !state->valid) {
        LOG_WARN("Invalid frame state");
        bufferPool_[index].setInUse(false);
        return dstFrame;
    }
    
    // 执行RGA格式转换
    im_rect rect = {0, 0, static_cast<int>(config_.width), static_cast<int>(config_.height)};
    RgaConverter::RgaParams params{src, rect, dst, rect};
    
    IM_STATUS status = RgaConverter::instance().FormatTransform(params);
    
    // 复制元数
    auto rgaMeta = rawFrame->meta;
    rgaMeta.index = index;
    
    // 释放原始
    rawFrame.reset();
    
    if (status != IM_STATUS_SUCCESS) {
        LOG_ERROR("RGA format conversion failed: %d", status);
        bufferPool_[index].setInUse(false);
        failedTasks_++;
        return dstFrame;
    }
    
    // 创建输出
    dstFrame.reset(new Frame(bufferPool_[index].getState()));
    dstFrame->meta = rgaMeta;
    dstFrame->setReleaseCallback<RgaProcessor, &RgaProcessor::releaseBuffer>(this);
    
    LOG_TRACE("Frame processed successfully: index=%d", index);
    
    return dstFrame;
}

void RgaProcessor::workerThreadMain() {
    LOG_INFO("RGA worker thread started (TID: %ld)", syscall(SYS_gettid));
    
    while (running_.load()) {
        // 检查暂停状
        pauser_.wait_if_paused();
        
        if (!running_.load()) {
            break;
        }
        
        auto queue = rawQueue_.lock();
        if (!queue) {
            std::unique_lock<std::mutex> lock(taskQueueMutex_);
            taskQueueCv_.wait_for(lock, std::chrono::milliseconds(5), [this] {
                return !running_.load(std::memory_order_relaxed);
            });
            continue;
        }

        if (queue->size_approx() == 0) {
            std::unique_lock<std::mutex> lock(taskQueueMutex_);
            taskQueueCv_.wait_for(lock, std::chrono::milliseconds(1), [this, &queue] {
                return !running_.load(std::memory_order_relaxed) ||
                       queue->size_approx() > 0;
            });
            continue;
        }

        {
            std::unique_lock<std::mutex> lock(taskQueueMutex_);
            taskQueueCv_.wait(lock, [this] {
                return !running_.load(std::memory_order_relaxed) ||
                       (inFlightTasks_.load(std::memory_order_relaxed) + readyFrames_.size()) <
                           static_cast<size_t>(config_.maxPendingTasks);
            });
            if (!running_.load(std::memory_order_relaxed)) {
                break;
            }
            inFlightTasks_.fetch_add(1, std::memory_order_relaxed);
        }

        if (!threadPool_->try_post(this, &RgaProcessor::processOneTask)) {
            inFlightTasks_.fetch_sub(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(taskQueueMutex_);
            taskQueueCv_.notify_one();
        }
    }
    
    LOG_INFO("RGA worker thread stopped");
}

void RgaProcessor::processOneTask() {
    FramePtr frame = processInference();
    {
        std::lock_guard<std::mutex> lock(taskQueueMutex_);
        if (frame) {
            readyFrames_.emplace_back(std::move(frame));
            completedTasks_.fetch_add(1, std::memory_order_relaxed);
        }
        inFlightTasks_.fetch_sub(1, std::memory_order_relaxed);
    }
    taskQueueCv_.notify_one();
}

int RgaProcessor::dump(FramePtr& frame, int64_t timeout) {
    frame = nullptr;
    
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);

    std::unique_lock<std::mutex> lock(taskQueueMutex_);
    while (readyFrames_.empty()) {
        if (!running_.load()) {
            LOG_TRACE("Not running, no ready frames");
            return -1;
        }
        if (taskQueueCv_.wait_until(lock, deadline) == std::cv_status::timeout) {
            LOG_TRACE("No ready frames within timeout");
            return -1;
        }
    }

    frame = std::move(readyFrames_.front());
    readyFrames_.pop_front();
    taskQueueCv_.notify_one();
    if (frame) {
        int index = frame->meta.index;
        LOG_TRACE("Dumped frame with index %d", index);
        return index;
    }
    LOG_TRACE("Ready frame is null");
    return -1;
}

void RgaProcessor::releaseBuffer(int index) {
    if (index < 0 || index >= static_cast<int>(bufferPool_.size())) {
        LOG_WARN("Invalid buffer index: %d", index);
        return;
    }
    
    bufferPool_[index].setInUse(false);
    LOG_TRACE("Buffer %d released", index);
}

bool RgaProcessor::setThreadAffinity(int cpuCore) {
    if (cpuCore < 0) {
        LOG_WARN("Invalid CPU core: %d", cpuCore);
        return false;
    }
    
    if (workerThread_.joinable()) {
        return ThreadUtils::safeBindThread(workerThread_, cpuCore);
    } else {
        LOG_WARN("Worker thread not running, cannot set affinity");
        return false;
    }
}

RgaProcessor::BufferPoolStats RgaProcessor::getBufferPoolStats() const {
    std::lock_guard<std::mutex> lock(bufferPoolMutex_);
    
    BufferPoolStats stats{};
    stats.totalBuffers = bufferPool_.size();
    
    for (const auto& buffer : bufferPool_) {
        if (buffer.isInUse()) {
            stats.usedBuffers++;
        } else if (buffer.isValid()) {
            stats.availableBuffers++;
        }
    }
    
    if (stats.totalBuffers > 0) {
        stats.usagePercentage = (static_cast<double>(stats.usedBuffers) / stats.totalBuffers) * 100.0;
    }
    
    return stats;
}

RgaProcessor::TaskQueueStats RgaProcessor::getTaskQueueStats() const {
    std::lock_guard<std::mutex> lock(taskQueueMutex_);
    
    TaskQueueStats stats{};
    stats.pendingTasks = readyFrames_.size() + inFlightTasks_.load(std::memory_order_relaxed);
    stats.completedTasks = completedTasks_.load();
    stats.failedTasks = failedTasks_.load();
    
    return stats;
}

void RgaProcessor::cleanupPendingTasks() {
    std::lock_guard<std::mutex> lock(taskQueueMutex_);
    
    size_t cleaned = 0;
    cleaned = readyFrames_.size();
    readyFrames_.clear();
    
    if (cleaned > 0) {
        LOG_INFO("Cleaned up %zu pending tasks", cleaned);
    }
}

bool RgaProcessor::dumpDmabufAsXXXX8888(int dmabuf_fd, uint32_t width, uint32_t height,
                                       uint32_t size, uint32_t pitch, const char* path) {
    if (dmabuf_fd < 0 || width == 0 || height == 0 || !path) {
        LOG_ERROR("Invalid parameters for DMA-BUF dump");
        return false;
    }
    
    // 打开文件
    int file_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (file_fd < 0) {
        LOG_ERROR("Failed to open file %s: %s", path, strerror(errno));
        return false;
    }
    
    // 映射DMA-BUF
    void* mapped = mmap(nullptr, size, PROT_READ, MAP_SHARED, dmabuf_fd, 0);
    if (mapped == MAP_FAILED) {
        LOG_ERROR("Failed to map DMA-BUF: %s", strerror(errno));
        close(file_fd);
        return false;
    }
    
    // 写入文件
    bool success = false;
    try {
        // 逐行写入
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* row = static_cast<const uint8_t*>(mapped) + y * pitch;
            ssize_t written = write(file_fd, row, width * 4); // RGBA8888: 4 bytes per pixel
            if (written != static_cast<ssize_t>(width * 4)) {
                LOG_ERROR("Failed to write row %u to file", y);
                throw std::runtime_error("Write failed");
            }
        }
        success = true;
        LOG_INFO("DMA-BUF dumped to %s (size: %ux%u)", path, width, height);
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during DMA-BUF dump: %s", e.what());
    }
    
    // 清理
    munmap(mapped, size);
    close(file_fd);
    
    return success;
}

// 向后兼容的全局函数
extern "C" {
    
RgaProcessor* create_rga_processor(const RgaProcessorConfig* config) {
    try {
        return new RgaProcessor(*config);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create RgaProcessor: %s", e.what());
        return nullptr;
    }
}

void destroy_rga_processor(RgaProcessor* processor) {
    delete processor;
}

int rga_processor_start(RgaProcessor* processor) {
    if (!processor) return static_cast<int>(RgaProcessorError::INVALID_CONFIG);
    return static_cast<int>(processor->start());
}

int rga_processor_stop(RgaProcessor* processor) {
    if (!processor) return static_cast<int>(RgaProcessorError::INVALID_CONFIG);
    return static_cast<int>(processor->stop());
}

int rga_processor_dump(RgaProcessor* processor, FramePtr* frame, int64_t timeout) {
    if (!processor || !frame) return -1;
    return processor->dump(*frame, timeout);
}

void rga_processor_release_buffer(RgaProcessor* processor, int index) {
    if (processor) {
        processor->releaseBuffer(index);
    }
}

} // extern "C"
