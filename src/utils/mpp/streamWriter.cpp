/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-11-21 20:04:56
 * @FilePath: /src/utils/mpp/streamWriter.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */

/**
 * @file StreamWriter.cpp
 * @brief StreamWriter 实现(中文注释, Doxygen 风格)
 *
 * 行为说明: 
 * - 构造时拆分文件名并打开第一个分段文件(writerA)
 * - 启动调度线程与两个写线程(writerA / writerB)
 * - pushMeta 将 meta 放到调度队列, 返回立即返回(非阻塞)
 * - 调度线程分发 meta 到 currentWriter_, 并在达到 packetsPerSegment_ 时切片并切换到 idleWriter_
 * - 写线程从其队列中取出 meta, 等待对应的 EncodedPacket 可用后写入文件并 releaseSlot
 *
 * 线程安全要点: 
 * - dispatchQueue_ 有独立互斥保护
 * - 每个 writer 的 queue 有独立互斥保护
 * - 与 MppEncoderCore 的交互依赖其 tryGetEncodedPacket/ releaseSlot 的线程安全性
 */

#include "mpp/streamWriter.h"

#include <cstdio>
#include <fcntl.h>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cerrno>

#include "logger_v2.h"

using namespace std::chrono_literals;

// 文件名拆分
static std::string makeSegmentFilename(const std::string &base, size_t idx, const std::string &suffix) {
    // 构造形如 base_0001.suf 的文件名, 宽度 4
    char buf[512];
    snprintf(buf, sizeof(buf), "%s_%04zu%s", base.c_str(), idx, suffix.c_str());
    return std::string(buf);
}

// -------------------- FileGuard --------------------
StreamWriter::FileGuard::FileGuard(FILE * f) {
    reset(f);
}

StreamWriter::FileGuard::~FileGuard() {
    if (nullptr != fp_) {
        fclose(fp_);
        fp_ = nullptr;
    }
}

void StreamWriter::FileGuard::reset(FILE *f) {
    if (fp_) {
        fflush(fp_); // 保证写盘
        if (fd >= 0) {
            posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        }
        fclose(fp_);
    }
    fp_ = f;
    fd = fp_ ? fileno(fp_) : -1;
    if (fd >= 0) {
        posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    }
}

// -------------------- 构造/初始化/析构 --------------------
/**
 * @brief 构造函数: 拆分文件名, 打开第一个分段, 启动线程
 */
StreamWriter::StreamWriter(const std::string &path)
    : originalPath_(path),
      running_(true),
      acceptingMeta_(true),
      firstKeyFramePending_(true)
{
    // 拆分 baseName_ 与 suffix_
    size_t pos = originalPath_.find_last_of('.');
    if (std::string::npos == pos) { // 无后缀
        baseName_ = originalPath_;
        suffix_ = "";
    } else {
        baseName_ = originalPath_.substr(0, pos);
        suffix_ = originalPath_.substr(pos);
    }

    // 默认 currentWriter_ 为 writerA_, idleWriter_ 为 writerB_
    currentWriter_ = &writerA_;
    idleWriter_ = &writerB_;

    // 打开第一个分段到 currentWriter_
    if (!currentWriter_) {
        LOG_ERROR("[StreamWriter] Cannot get [currentWriter] from [writer].");
        return;
    }
    // 失败也继续启动线程(但写入会失败)
    if (!openNewSegmentFor(currentWriter_)) {
        LOG_ERROR("[StreamWriter] Cannot open segment file.");
    }

    // 启动线程
    if (!initThreads()) {
        LOG_ERROR("[StreamWriter] Writer thread initialization failed.");
        return;
    }
    LOG_INFO("[StreamWriter] Writer thread initialized successfully:");
    LOG_INFO("\tbase=%s", baseName_.c_str());
    LOG_INFO("\tsuffix=%s", suffix_.c_str());
}

/**
 * @brief 析构: 关闭线程并清理资源
 */
StreamWriter::~StreamWriter() {
    stop();
    // writer 的 FileGuard 会在 unique_ptr 析构时关闭文件
}

void StreamWriter::stop() {
    if (!acceptingMeta_.exchange(false)) {
        return;
    }
    // 先停收新 meta, 再等待 dispatch 把已有队列全部转交给 writer.
    // 只有 writer 的 pendingCount 清零后才会真正让线程退出, 这样可以最大化保留已编码结果.
    dispatchCv_.notify_all();
    writerA_.cv.notify_all();
    writerB_.cv.notify_all();
    if (dispatchThread_.joinable()) dispatchThread_.join();
    waitUntilWriterIdle(&writerA_);
    waitUntilWriterIdle(&writerB_);
    running_.store(false);
    writerA_.cv.notify_all();
    writerB_.cv.notify_all();
    if (writerThreadA_.joinable()) writerThreadA_.join();
    if (writerThreadB_.joinable()) writerThreadB_.join();
    LOG_INFO("[StreamWriter] Stop...");
}

/**
 * @brief 打开新的分段文件并绑定到指定 writer 上
 * @param ctx 目标 writer 上下文
 * @return true 打开成功
 */
bool StreamWriter::openNewSegmentFor(WriterCtx* ctx) {
    if (nullptr == ctx) return false;

    // 更换文件前必须保证该 writer 已完全写空, 否则会把新旧 segment 混写到同一文件句柄上.
    waitUntilWriterIdle(ctx);

    std::string filename = makeSegmentFilename(baseName_, segmentIndex_, suffix_);

    FILE *fp = fopen(filename.c_str(), "wb");
    if (nullptr == fp) {
        LOG_ERROR("[StreamWriter] Failed to open file: %s", filename.c_str());
        return false;
    }

    if (ctx->fp == nullptr) { // 第一次需要构造
        ctx->fp = std::make_unique<FileGuard>(fp);
    } else { // 给 writer 设置新的 FileGuard(旧fp自动关闭)
        ctx->fp->reset(fp);
    }

    // 获取对应 fd
    if ((ctx->fd = fileno(fp)) < 0){
        LOG_ERROR("[StreamWriter] Failed to get [%s] file descriptor.", filename.c_str());
        return false;
    }
    // 顺序访问优化
    posix_fadvise(ctx->fd, 0, 0, POSIX_FADV_SEQUENTIAL);
    return true;
}

/**
 * @brief 启动调度线程与两个写线程
 * @return true 成功
 * @return false 失败
 */
bool StreamWriter::initThreads() {
    try {
        dispatchThread_ = std::thread(&StreamWriter::dispatchLoop, this);
        writerThreadA_ = std::thread(&StreamWriter::writerLoop, this, &writerA_);
        writerThreadB_ = std::thread(&StreamWriter::writerLoop, this, &writerB_);
    } catch (const std::system_error &e) {
        LOG_ERROR("[StreamWriter] Thread initialization failed: %s", e.what());
        return false;
    }
    return true;
}

//  -------------------- 调用者接口 --------------------
/**
 * @brief 将 meta 放入调度队列
 * @param meta EncodedMeta
 * @return true 成功
 */
bool StreamWriter::pushMeta(const MppEncoderCore::EncodedMeta &meta) {
    if (!acceptingMeta_) {
        return false;
    }
    if (nullptr == meta.core) {
        LOG_ERROR("[StreamWriter] pushMeta: meta.core is nullptr.");
        return false;
    }
    // 分发到当前 writer
    {
        std::lock_guard<std::mutex> lk(dispatchMtx_);
        dispatchQueue_.push(meta);
    }
    dispatchCv_.notify_all();
    return true;
}

// -------------------- 内部实现 --------------------
/**
 * @brief 写线程主循环
 * @param ctx 写线程上下文指针
 *
 * 写线程职责: 
 * - 等待 ctx->queue 有数据
 * - 对每个 meta 调用 obtainPacketForMeta 获取 packet
 * - 将 packet 写入 ctx->fp
 * - 调用 meta.core->releaseSlot 释放 slot
 */
void StreamWriter::writerLoop(WriterCtx *ctx) {
    if (nullptr == ctx) return;
    constexpr size_t FLUSH_THRESHOLD = 1024 * 1024 * 2; // 2MB
    size_t accumulatedSize{0};

    while (true) {
        MppEncoderCore::EncodedPacketPtr packet;
        MppEncoderCore::EncodedMeta meta;
        {
            std::unique_lock<std::mutex> lk(ctx->mtx);
            ctx->cv.wait(lk, [&] {
                return !ctx->queue.empty() || !running_;
            });
            if (ctx->queue.empty() && !running_) {
                break;
            }
            // 取出meta
            if (ctx->queue.empty()) continue;
            meta = ctx->queue.front();
            ctx->queue.pop();
        }
        SlotGuard guard(meta);
        if (meta.packet) {
            packet = meta.packet;
        } else if (!obtainPacketForMeta(packet, meta)) {
            std::lock_guard<std::mutex> lk(ctx->mtx);
            if (ctx->pendingCount > 0) {
                --ctx->pendingCount;
            }
            if (ctx->pendingCount == 0 && ctx->queue.empty()) {
                ctx->idleCv.notify_all();
            }
            continue;
        }
        // 写入当前文件 (短路求值)
        if (nullptr == ctx->fp || nullptr == ctx->fp->get()) {
            LOG_ERROR("[StreamWriter] writerLoop: File point to NULL, drop current packet.");
            std::lock_guard<std::mutex> lk(ctx->mtx);
            if (ctx->pendingCount > 0) {
                --ctx->pendingCount;
            }
            if (ctx->pendingCount == 0 && ctx->queue.empty()) {
                ctx->idleCv.notify_all();
            }
            continue;
        }
        // LOG_INFO("[SlotGuard] Acquired slot_id=%d\n", meta.slot_id); 
        void* data = packet->data();
        if (nullptr == data) {
            LOG_ERROR("[StreamWriter] writerLoop: Packet data is NULL, drop current packet.");
            std::lock_guard<std::mutex> lk(ctx->mtx);
            if (ctx->pendingCount > 0) {
                --ctx->pendingCount;
            }
            if (ctx->pendingCount == 0 && ctx->queue.empty()) {
                ctx->idleCv.notify_all();
            }
            continue;
        }
        // 写入文件
        FILE* fp = ctx->fp->get();
        if (!fp) continue;
        size_t length = packet->length();

        // 写入到缓冲区
        size_t written = fwrite(data, 1, length, fp);
        
        if (written != length) {
            LOG_ERROR("[StreamWriter] Insufficient actual data written: %zu/%zu", written, length);
        }
        
        // 更新累加大小
        accumulatedSize += written;
        
        // 达到阈值时刷新并重置累加器
        if (accumulatedSize >= FLUSH_THRESHOLD && fflush(fp) != 0) {
            LOG_ERROR("[StreamWriter] Flush STREAM error:%s", std::strerror(errno));
        }
        if (accumulatedSize >= FLUSH_THRESHOLD) {
            accumulatedSize = 0;
        }
        {
            std::lock_guard<std::mutex> lk(ctx->mtx);
            if (ctx->pendingCount > 0) {
                --ctx->pendingCount;
            }
            if (ctx->pendingCount == 0 && ctx->queue.empty()) {
                ctx->idleCv.notify_all();
            }
        }
        // SlotGuard自动释放
    }
    if (ctx->fp && ctx->fp->get()) {
        fflush(ctx->fp->get());
    }
    // 线程退出时自动由 FileGuard 关闭文件
}

/**
 * @brief 调度线程主循环: 从 dispatchQueue_ 取出 meta, 分发给 currentWriter_
 *        并在达到 packetsPerSegment_ 时切换到 idleWriter_
 */
void StreamWriter::dispatchLoop() {
    LOG_INFO("[StreamWriter] Start form segment index: 1");
    while (true) {
        MppEncoderCore::EncodedMeta meta;
        {
            std::unique_lock<std::mutex> lk(dispatchMtx_);
            dispatchCv_.wait(lk, [&] {
                return !dispatchQueue_.empty() || !acceptingMeta_;
            });
            if (dispatchQueue_.empty() && !acceptingMeta_) {
                break;
            }
            // 取出 meta
            if (dispatchQueue_.empty()) {
                continue;
            }
            meta = dispatchQueue_.front();
            dispatchQueue_.pop();
            
        }
        MppEncoderCore::EncodedPacketPtr packet;
        bool shouldCutSegment = false;
        if (!obtainPacketForMeta(packet, meta)) {
            continue;
        }
        const bool isFirstPacket = firstKeyFramePending_.load(std::memory_order_acquire);
        // 非关键帧不计数
        if (packet->isKeyframe()) {
            LOG_INFO("GET I(ntra) frame.");
            firstKeyFramePending_.store(false, std::memory_order_release);
            // 关键帧计数
            auto ct = currentPacketCount_.fetch_add(1, std::memory_order_release) + 1;
            // 检测是否切片
            if(ct >= packetsPerSegment_){
                shouldCutSegment = true;
            }
        } else if (isFirstPacket) {
            // Some older MPP builds do not populate KEY_OUTPUT_INTRA.
            // Do not block the first segment forever waiting for a flag that never arrives.
            LOG_WARN("[StreamWriter] first packet has no keyframe flag; start writing first segment anyway");
            firstKeyFramePending_.store(false, std::memory_order_release);
        }
        meta.packet = packet;
        
        // 达到切片条件
        if (shouldCutSegment){
            WriterCtx* writerToSync = nullptr;
            ++segmentIndex_;
            LOG_INFO("[StreamWriter] Switching to segment index: %zu", segmentIndex_);
            {
                std::lock_guard<std::mutex> lk2(writerSelectionMtx_);
                writerToSync = currentWriter_;
                // 新 segment 先在 idle writer 上准备好, 这样一旦 swap 完成, 后续包就能直接落到新文件.
                if (!openNewSegmentFor(idleWriter_)) {
                    LOG_ERROR("[StreamWriter] Failed to open on dispatch segment: %zu", segmentIndex_);
                }
                std::swap(currentWriter_, idleWriter_);
            }
            // 重置计数
            currentPacketCount_.store(0,  std::memory_order_relaxed);
            // 旧 writer 已经不再接收新包, 等它把最后几个包写完之后再刷盘和 fsync, 避免切片文件尾部丢失.
            waitUntilWriterIdle(writerToSync);
            flushAndSyncWriter(writerToSync);
        }
        // 只有在明确仍在等待首个可写包时才继续丢弃.
        if (firstKeyFramePending_.load(std::memory_order_acquire)) {
            if (meta.core != nullptr) {
                meta.core->releaseSlot(meta);
            }
            continue;
        }
        // 分发到 currentWriter_
        {
            std::lock_guard<std::mutex> writerLock(writerSelectionMtx_);
            std::lock_guard<std::mutex> queueLock(currentWriter_->mtx);
            ++currentWriter_->pendingCount;
            currentWriter_->queue.push(meta);
            currentWriter_->cv.notify_one();
        }
    }
}

/**
 * @brief 从 meta 获取对应的 EncodedPacket(等待一定重试次数)
 * @param out 输出 packet ptr
 * @param meta 输入 meta
 * @return true 成功获取
 * @return false 超时或出错(同时会释放 slot)
 */
bool StreamWriter::obtainPacketForMeta(MppEncoderCore::EncodedPacketPtr &out,
                                    MppEncoderCore::EncodedMeta &meta) {
    out = nullptr;
    if (nullptr == meta.core) {
        return false;
    }
    auto id = meta.slot_id;
    int tries = 4000;
    while (--tries && running_) {
        bool ok = meta.core->tryGetEncodedPacket(meta);
        if (!ok) {
            std::this_thread::sleep_for(250us);
            continue;
        }
        out = meta.packet;
        if (out == nullptr) {
            // 安全检查
            LOG_ERROR("[StreamWriter] obtainPacketForMeta: packet is nullptr, slot_id=%d", id);
            break;
        }
        return true;
    }
    if (tries <= 0) {
        LOG_ERROR("[StreamWriter][slot_id:%d] Timeout, packet dropped", id);
    }
    meta.core->releaseSlot(meta);
    return false;
}

bool StreamWriter::flushAndSyncWriter(WriterCtx *ctx) {
    if (ctx == nullptr || ctx->fp == nullptr || ctx->fp->get() == nullptr) {
        return true;
    }
    std::lock_guard<std::mutex> lk(ctx->mtx);
    FILE* fp = ctx->fp->get();
    if (fflush(fp) != 0) {
        LOG_ERROR("[StreamWriter] Flush STREAM error:%s", std::strerror(errno));
        return false;
    }
    if (ctx->fd >= 0 && fsync(ctx->fd) != 0) {
        LOG_ERROR("[StreamWriter] Failed to sync segment to disk: %d", errno);
        return false;
    }
    return true;
}

void StreamWriter::waitUntilWriterIdle(WriterCtx *ctx) {
    if (ctx == nullptr) {
        return;
    }
    std::unique_lock<std::mutex> lk(ctx->mtx);
    ctx->idleCv.wait(lk, [ctx] {
        return ctx->pendingCount == 0 && ctx->queue.empty();
    });
}
