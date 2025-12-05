#pragma once
/**
 * @file StreamWriter.h
 * @brief 双线程分段写入器头文件
 *
 * 该类实现: 
 * - 将编码器生成的 EncodedMeta 推入到写入队列
 * - 调度线程按顺序分发 meta 给当前写线程(writerA / writerB)
 * - 每达到固定 packet 数(默认 100)切换到另一个写线程并创建新文件段
 *
 * 文件命名: 
 *   输入 "output.h264" -> 产生 "output_001.h264", "output_002.h264", ...
 *
 * 设计要点: 
 * - 调度线程负责切片决策与分发(保证写线程职责单一)
 * - 写线程负责获取 packet, 写入并释放 slot
 * - 使用 FILEGuard 自动管理 FILE* 生命周期
 *
 * @author SweerItTer
 * @date 2025-11-21
 */

#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <cstdio>

#include "mpp/encoderCore.h"

/// 默认每片 packet 数量(根据I帧适应) (每N个I帧切一次)
static constexpr size_t STREAMWRITER_DEFAULT_I_PACKETS_PER_SEGMENT = 60;

/// 写线程数量(固定为 2)
static constexpr size_t STREAMWRITER_WRITER_COUNT = 2;

class StreamWriter {
public:
    /**
     * @brief 构造函数
     * @param path 输出基础路径, 例如 "output.h264"
     * @note 构造后会启动调度线程与写线程
     */
    explicit StreamWriter(const std::string &path);

    /**
     * @brief 析构函数
     * @note 会安全停止所有线程并关闭文件
     */
    ~StreamWriter();

    /**
     * @brief 推入编码结果 meta
     * @param meta 来自 MppEncoderCore 的 EncodedMeta
     * @return true 推入成功
     * @return false meta 无效或写器已停止
     */
    bool pushMeta(const MppEncoderCore::EncodedMeta &meta);
    
    /** @brief 停止写线程与调度线程 */
    void stop();
private:
    // 禁用复制
    StreamWriter(const StreamWriter &) = delete;
    StreamWriter &operator=(const StreamWriter &) = delete;

private:
    /**
     * @brief RAII管理的文件句柄保护类
     */
    class FILEGuard {
    public:
        explicit FILEGuard(FILE *f);
        ~FILEGuard();
        FILE *get() const { return fp_; }
        void reset(FILE *f);
    private:
        FILE *fp_ = nullptr;
        int fd = 0;
    };

    /**
     * @brief 写线程上下文
     * 每个 writer 都有独立队列, 互斥, 条件变量以及当前段文件指针
     */
    struct WriterCtx {
        std::queue<MppEncoderCore::EncodedMeta> queue; ///< 待写 meta 队列
        std::mutex mtx;                                ///< 队列保护锁
        std::condition_variable cv;                    ///< 唤醒条件
        std::unique_ptr<FILEGuard> fp;                 ///< 当前写入段的文件指针
        // std::atomic<bool> iscurrent{false};            ///< 是否为当前写入线程
    };

private:
    // 初始化/线程函数
    bool initThreads();
    void dispatchLoop();                       ///< 调度线程主循环
    void writerLoop(WriterCtx *ctx);           ///< 写线程主循环
    bool openNewSegmentFor(WriterCtx *ctx);    ///< 为指定 writer 打开下一个分段文件
    bool obtainPacketForMeta(MppEncoderCore::EncodedPacketPtr &packet,
                             MppEncoderCore::EncodedMeta &meta); ///< 等待并获取 packet

private:
    // 文件名处理
    std::string originalPath_;  ///< 用户输入的 path
    std::string baseName_;      ///< 文件基础名(不含后缀)
    std::string suffix_;        ///< 文件后缀(含点), 如 ".h264"

    // 切片控制
    size_t packetsPerSegment_ = STREAMWRITER_DEFAULT_I_PACKETS_PER_SEGMENT;
    std::atomic<size_t> currentPacketCount_{0}; ///< 当前片已经写入的 packet 数
    size_t segmentIndex_ = 1;       ///< 当前片索引, 从 1 开始
    
    // 调度与写队列
    std::queue<MppEncoderCore::EncodedMeta> dispatchQueue_;
    std::mutex dispatchMtx_;
    std::condition_variable dispatchCv_;

    // 两个 writer 上下文
    WriterCtx writerA_;
    WriterCtx writerB_;
    WriterCtx *currentWriter_ = nullptr; ///< 正在被分发写入的 writer
    WriterCtx *idleWriter_ = nullptr;    ///< 空闲 writer(下一段将使用)

    // 线程
    std::thread dispatchThread_;
    std::thread writerThreadA_;
    std::thread writerThreadB_;

    // 运行开关
    std::atomic<bool> running_{true};
};