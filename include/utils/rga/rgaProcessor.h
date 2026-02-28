/*
 * @FilePath: /include/utils/rga/rgaProcessor.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-17 22:51:16
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef RGAPROCESSTHREAD_H
#define RGAPROCESSTHREAD_H

#include <atomic>
#include <thread>
#include <memory>
#include <queue>
#include <future>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <deque>

#include "types.h"
#include "v4l2/cameraController.h"
#include "rga/rgaConverter.h"
#include "asyncThreadPool.h"
#include "dma/dmaBuffer.h"
#include "rga/formatTool.h"
#include "threadPauser.h"

namespace utils {
namespace rga {

/**
 * @brief RGA处理器错误码
 */
enum class RgaProcessorError : int {
    SUCCESS = 0,
    INVALID_CONFIG = -1,
    BUFFER_POOL_FULL = -2,
    NO_AVAILABLE_BUFFER = -3,
    INVALID_FRAME = -4,
    RGA_CONVERSION_FAILED = -5,
    THREAD_ALREADY_RUNNING = -6,
    THREAD_NOT_RUNNING = -7
};

/**
 * @brief RGA缓冲区 - 使用RAII管理资源
 */
class RgbaBuffer {
public:
    RgbaBuffer() = default;
    
    /**
     * @brief 构造函数 - 创建DMA-BUF缓冲区
     */
    RgbaBuffer(std::shared_ptr<DmaBuffer> dmaBuf);
    
    /**
     * @brief 构造函数 - 创建MMAP缓冲区
     */
    RgbaBuffer(void* data, size_t size);
    
    /**
     * @brief 析构函数 - 自动释放资源
     */
    ~RgbaBuffer();
    
    // 禁用拷贝构造和赋值
    RgbaBuffer(const RgbaBuffer&) = delete;
    RgbaBuffer& operator=(const RgbaBuffer&) = delete;
    
    // 允许移动语义
    RgbaBuffer(RgbaBuffer&& other) noexcept;
    RgbaBuffer& operator=(RgbaBuffer&& other) noexcept;
    
    /**
     * @brief 检查缓冲区是否有效
     */
    bool isValid() const noexcept;
    
    /**
     * @brief 检查缓冲区是否正在使用
     */
    bool isInUse() const noexcept { return in_use_.load(std::memory_order_relaxed); }
    
    /**
     * @brief 设置缓冲区使用状态
     */
    void setInUse(bool inUse) noexcept { in_use_.store(inUse, std::memory_order_relaxed); }
    
    /**
     * @brief 获取共享缓冲区状态
     */
    std::shared_ptr<SharedBufferState> getState() const noexcept { return state_; }
    
    /**
     * @brief 获取DMA-BUF文件描述符(仅DMA-BUF模式)
     */
    int getFd() const noexcept;
    
    /**
     * @brief 获取虚拟地址(仅MMAP模式)
     */
    void* getVirtualAddress() const noexcept;
    
    /**
     * @brief 获取缓冲区大小
     */
    size_t getSize() const noexcept;
    
    /**
     * @brief 获取内存类型
     */
    Frame::MemoryType getMemoryType() const noexcept { return memory_type_; }

private:
    std::shared_ptr<SharedBufferState> state_;
    std::atomic<bool> in_use_{false};
    Frame::MemoryType memory_type_ = Frame::MemoryType::MMAP;
};

/**
 * @brief RGA处理器配置结构体
 */
struct RgaProcessorConfig {
    std::weak_ptr<FrameQueue> rawQueue;          // 原始帧队列(弱引用)
    uint32_t width = 0;                          // 图像宽度
    uint32_t height = 0;                         // 图像高度
    bool usingDMABUF = false;                    // 是否使用DMA-BUF
    int dstFormat = RK_FORMAT_RGBA_8888;         // 目标格式
    int srcFormat = RK_FORMAT_YCbCr_420_SP;      // 源格式
    int poolSize = 4;                            // 缓冲池大小
    int threadAffinity = -1;                     // 线程亲和性(-1表示不设置)
    int maxPendingTasks = 30;                    // 最大挂起任务数
    
    /**
     * @brief 验证配置是否有效
     */
    bool isValid() const noexcept;
    
    /**
     * @brief 获取配置描述字符串
     */
    std::string toString() const;
};

/**
 * @brief RGA处理器 - 高性能图像处理流水线
 * 
 * 设计模式: 
 * - 生产者-消费者模式: 从原始帧队列消费, 生产到输出缓冲池
 * - 对象池模式: 使用RgbaBuffer向量管理输出缓冲区
 * - 异步处理: 使用AsyncThreadPool异步执行RGA处理
 * - 策略模式: 支持DMA-BUF和MMAP两种内存类型
 */
class RgaProcessor {
public:
    /**
     * @brief 构造函数
     * @param config 处理器配置
     * @throws std::invalid_argument 如果配置无效
     * @throws std::runtime_error 如果初始化失败
     */
    explicit RgaProcessor(const RgaProcessorConfig& config);
    
    /**
     * @brief 析构函数 - 自动停止并清理资源
     */
    ~RgaProcessor();
    
    // 禁用拷贝构造和赋值
    RgaProcessor(const RgaProcessor&) = delete;
    RgaProcessor& operator=(const RgaProcessor&) = delete;
    
    /**
     * @brief 启动处理线程
     * @return RgaProcessorError::SUCCESS 成功, 其他值表示错误
     */
    RgaProcessorError start();
    
    /**
     * @brief 停止处理线程
     * @return RgaProcessorError::SUCCESS 成功, 其他值表示错误
     */
    RgaProcessorError stop();
    
    /**
     * @brief 暂停处理
     */
    void pause();
    
    /**
     * @brief 恢复处理
     */
    void resume();
    
    /**
     * @brief 获取处理后的帧
     * @param frame 输出参数, 接收处理后的FramePtr
     * @param timeout 超时时间(毫秒, 默认5000ms)
     * @return 成功返回缓冲区索引(>=0), 失败返回错误码(<0)
     */
    int dump(FramePtr& frame, int64_t timeout = 5000);
    
    /**
     * @brief 释放缓冲区
     * @param index 缓冲区索引
     */
    void releaseBuffer(int index);
    
    /**
     * @brief 设置线程亲和性
     * @param cpuCore CPU核心编号(0-based)
     * @return true如果设置成功, false否则
     */
    bool setThreadAffinity(int cpuCore);
    
    /**
     * @brief 获取处理器状态
     */
    bool isRunning() const noexcept { return running_; }
    
    /**
     * @brief 获取缓冲池使用情况
     */
    struct BufferPoolStats {
        size_t totalBuffers;
        size_t usedBuffers;
        size_t availableBuffers;
        double usagePercentage;
    };
    
    BufferPoolStats getBufferPoolStats() const;
    
    /**
     * @brief 获取任务队列状态
     */
    struct TaskQueueStats {
        size_t pendingTasks;
        size_t completedTasks;
        size_t failedTasks;
    };
    
    TaskQueueStats getTaskQueueStats() const;
    
    /**
     * @brief 静态工具函数: 将DMA-BUF保存为文件
     */
    static bool dumpDmabufAsXXXX8888(int dmabuf_fd, uint32_t width, uint32_t height, 
                                     uint32_t size, uint32_t pitch, const char* path);

private:
    /**
     * @brief 初始化缓冲池
     * @throws std::runtime_error 如果初始化失败
     */
    void initBufferPool();
    
    /**
     * @brief 获取可用缓冲区索引
     * @return 可用缓冲区索引, -1表示无可用缓冲区
     */
    int getAvailableBufferIndex();
    
    /**
     * @brief DMA-BUF帧处理
     */
    int processDmaBufFrame(rga_buffer_t& src, rga_buffer_t& dst, 
                          std::shared_ptr<DmaBuffer> srcDmaBuf);
    
    /**
     * @brief MMAP帧处理
     */
    int processMmapFrame(rga_buffer_t& src, rga_buffer_t& dst, void* data);
    
    /**
     * @brief 自动选择处理方式
     */
    int processFrameAuto(rga_buffer_t& src, rga_buffer_t& dst, 
                        std::weak_ptr<Frame> frame);
    
    /**
     * @brief 实际处理函数
     */
    FramePtr processInference();
    
    /**
     * @brief 工作线程主循环
     */
    void workerThreadMain();
    
    /**
     * @brief 验证帧格式
     */
    bool validateFrameFormat(const FramePtr& frame) const;
    
    /**
     * @brief 清理挂起的任务
     */
    void cleanupPendingTasks();
    
    // 配置参数
    RgaProcessorConfig config_;
    
    // 线程控制
    std::atomic_bool running_{false};
    std::atomic_bool stopping_{false};
    ThreadPauser pauser_;
    std::thread workerThread_;
    std::mutex workerMutex_;
    std::condition_variable workerCV_;
    
    // 缓冲池管理
    std::vector<RgbaBuffer> bufferPool_;
    std::atomic<int> currentIndex_{0};
    mutable std::mutex bufferPoolMutex_;
    
    // 任务队列管理
    std::unique_ptr<asyncThreadPool> threadPool_;
    std::deque<std::future<FramePtr>> pendingTasks_;
    mutable std::mutex taskQueueMutex_;
    std::condition_variable taskQueueCv_;
    std::atomic<size_t> completedTasks_{0};
    std::atomic<size_t> failedTasks_{0};
    
    // 帧队列
    std::weak_ptr<FrameQueue> rawQueue_;
    
    // 处理状态
    Frame::MemoryType frameType_;
};

} // namespace rga
} // namespace utils

// 向后兼容的全局定义
using RgaProcessor = utils::rga::RgaProcessor;
using RgaProcessorConfig = utils::rga::RgaProcessorConfig;

#endif // RGAPROCESSTHREAD_H
