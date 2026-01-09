/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-11-21 20:04:56
 * @FilePath: /EdgeVision/src/utils/mpp/streamWriter.cpp
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
#include <iostream>

using namespace std::chrono_literals;

// 文件名拆分
static std::string makeSegmentFilename(const std::string &base, size_t idx, const std::string &suffix) {
    // 构造形如 base_0001.suf 的文件名, 宽度 4
    char buf[512];
    snprintf(buf, sizeof(buf), "%s_%04zu%s", base.c_str(), idx, suffix.c_str());
    return std::string(buf);
}

static void extract_sps_pps(const uint8_t* data, size_t len, std::vector<uint8_t>& out) {
    out.clear();

    size_t i = 0;
    while (i + 4 < len) {
        // 找起始码 00 00 00 01
        if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 1) {
            size_t nal_start = i + 4;
            uint8_t nal_type = data[nal_start] & 0x1F;

            // 找下一个起始码
            size_t j = i + 4;
            while (j + 4 < len &&
                !(data[j] == 0 && data[j+1] == 0 && data[j+2] == 0 && data[j+3] == 1)) {
                j++;
            }

            size_t nal_end = j;

            // SPS = 7, PPS = 8
            if (nal_type == 7 || nal_type == 8) {
                out.insert(out.end(), data + i, data + nal_end);
            }

            i = j;
        } else {
            i++;
        }
    }
}

// -------------------- FILEGuard --------------------
StreamWriter::FILEGuard::FILEGuard(FILE * f) {
    reset(f);
}

StreamWriter::FILEGuard::~FILEGuard() {
    if (nullptr != fp_) {
        fclose(fp_);
        fp_ = nullptr;
    }
}

void StreamWriter::FILEGuard::reset(FILE *f) {
    if (fp_) {
        fflush(fp_); // 保证写盘
        posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        fclose(fp_);
    }
    fp_ = f;
    fd = fileno(fp_);
    posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
}

// -------------------- 构造/初始化/析构 --------------------
/**
 * @brief 构造函数: 拆分文件名, 打开第一个分段, 启动线程
 */
StreamWriter::StreamWriter(const std::string &path)
    : originalPath_(path), firstIframeNeed(true), running_(true)
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
        fprintf(stderr, "[StreamWriter] Cannot get [currentWriter] from [writer].\n");
        return;
    }
    // 失败也继续启动线程(但写入会失败)
    if (!openNewSegmentFor(currentWriter_)) {
        fprintf(stderr, "[StreamWriter] Cannot open segment file.\n");
    }

    // 启动线程
    if (!initThreads()) {
        fprintf(stderr, "[StreamWriter] Writer thread initialization failed.\n");
        return;
    }
    fprintf(stdout, "[StreamWriter] Writer thread initialized successfully:\n\tbase=%s\n\tsuffix=%s\n",
            baseName_.c_str(), suffix_.c_str());
}

/**
 * @brief 析构: 关闭线程并清理资源
 */
StreamWriter::~StreamWriter() {
    stop();
    // writer 的 FILEGuard 会在 unique_ptr 析构时关闭文件
}

void StreamWriter::stop() {
    // 唤醒所有等待写线程
    if (!running_.exchange(false)) return;
    do{
        // 写完数据        
        writerA_.cv.notify_all();
        writerB_.cv.notify_all();
        dispatchCv_.notify_all();
        dispatchCv_.notify_all();
        writerA_.cv.notify_all();
        writerB_.cv.notify_all();
    } while (!dispatchQueue_.empty() && !writerA_.queue.empty() && !writerB_.queue.empty());  

    // join
    if (dispatchThread_.joinable()) dispatchThread_.join();
    if (writerThreadA_.joinable()) writerThreadA_.join();
    if (writerThreadB_.joinable()) writerThreadB_.join();
    fprintf(stderr, "[StreamWriter] Stop...\n");
}

/**
 * @brief 打开新的分段文件并绑定到指定 writer 上
 * @param ctx 目标 writer 上下文
 * @return true 打开成功
 */
bool StreamWriter::openNewSegmentFor(WriterCtx* ctx) {
    if (nullptr == ctx) return false;

    std::string filename = makeSegmentFilename(baseName_, segmentIndex_, suffix_);

    FILE *fp = fopen(filename.c_str(), "wb");
    if (nullptr == fp) {
        fprintf(stderr, "[StreamWriter] Failed to open file: %s\n", filename.c_str());
        return false;
    }

    // 给 writer 设置新的 FILEGuard(旧fp自动关闭)
    if (ctx->fp == nullptr) {
        ctx->fp = std::make_unique<FILEGuard>(fp);
        return true;
    }
    ctx->fp->reset(fp);
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
        fprintf(stderr, "[StreamWriter] Thread initialization failed: %s\n", e.what());
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
    if (!running_) {
        return false;
    }
    if (nullptr == meta.core) {
        fprintf(stderr, "[StreamWriter] pushMeta: meta.core is nullptr.\n");
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

    while (running_) {        
        MppEncoderCore::EncodedPacketPtr packet;
        MppEncoderCore::EncodedMeta meta;
        {
            std::unique_lock<std::mutex> lk(ctx->mtx);
            ctx->cv.wait(lk, [&] { return !running_ || !ctx->queue.empty(); });
            if (!running_) break;
            // 取出meta
            if (ctx->queue.empty()) continue;
            meta = ctx->queue.front();
            ctx->queue.pop();
            packet = meta.packet;
        }
        SlotGuard guard(meta.core, meta.slot_id);
        // 写入当前文件 (短路求值)
        if (nullptr == ctx->fp || nullptr == ctx->fp->get()) {
            fprintf(stderr, "[StreamWriter] writerLoop: File point to NULL, drop current packet.\n");
            continue;
        }
        // fprintf(stdout, "[SlotGuard] Acquired slot_id=%d\n", meta.slot_id); 
        void* data = packet->data();
        if (nullptr == data) {
            fprintf(stderr, "[StreamWriter] writerLoop: Packet data is NULL, drop current packet.\n");
            continue;
        }
        // 写入文件
        FILE* fp = ctx->fp->get();
        if (!fp) continue;
        int fd = fileno(fp);
        size_t length = packet->length();

        // 顺序访问优化
        posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
        size_t written = fwrite(data, 1, length, fp);
             
        if (written != length) {
            fprintf(stderr, "[StreamWriter] Insufficient actual data written: %zu/%zu\n", written, length);
        }
        
        // 更新累加大小
        accumulatedSize += written;
        
        // 达到阈值时刷新并重置累加器
        if (accumulatedSize < FLUSH_THRESHOLD) {
            continue;
        }
        if (fflush(fp) < 0) {
            // 将缓冲数据推到内核缓冲区(需要马上可读才需要这一步)
            fprintf(stderr, "[StreamWriter] Flush STREAM error:%s\n", std::strerror(errno));
        }
        accumulatedSize = 0;
        // SlotGuard自动释放
    }
    // 线程退出时自动由 FILEGuard 关闭文件
}

/**
 * @brief 调度线程主循环: 从 dispatchQueue_ 取出 meta, 分发给 currentWriter_
 *        并在达到 packetsPerSegment_ 时切换到 idleWriter_
 */
void StreamWriter::dispatchLoop() {
    bool exp = firstIframeNeed.load();
    fprintf(stdout, "[StreamWriter] Start form segment index: 1\n");
    while (running_) {
        MppEncoderCore::EncodedMeta meta;
        {
            std::unique_lock<std::mutex> lk(dispatchMtx_);
            dispatchCv_.wait(lk, [&] { return !dispatchQueue_.empty() || !running_; });
            if (!running_) break;
            // 取出 meta
            if (dispatchQueue_.empty()) {
                std::cout << "dispatchQueue_ EMPTY" << std::endl; 
                continue;
            }
            meta = dispatchQueue_.front();
            dispatchQueue_.pop();
            
        }
        // meta有效时确保后续异常时释放
        SlotGuard guard(meta.core, meta.slot_id);
        // 获取 packet
        MppEncoderCore::EncodedPacketPtr packet;
        if (!obtainPacketForMeta(packet, meta)) {
            // 已在 obtainPacketForMeta 中 releaseSlot
            continue;
        }
        bool cutSigment = false;
        // 非关键帧不计数
        if (packet->isKeyframe()) {
            fprintf(stdout, "GET I(ntra) frame.\n");
            firstIframeNeed.compare_exchange_weak(exp, false);
            // 关键帧计数
            auto ct = currentPacketCount_.fetch_add(1, std::memory_order_release) + 1;
            // 检测是否切片
            if(ct >= packetsPerSegment_){
                cutSigment = true;     
            }
        }
        
        // 达到切片条件
        if (cutSigment){
            ++segmentIndex_;
            fprintf(stdout, "[StreamWriter] Switching to segment index: %zu\n", segmentIndex_);
            // 为新的 currentWriter_ 打开分段文件(若打开失败, 继续使用旧文件)
            if (!openNewSegmentFor(idleWriter_)) {
                fprintf(stderr, "[StreamWriter] Failed to open on dispatch segment: %zu\n", segmentIndex_);
            }
            // 交换 writer
            {
                std::lock_guard<std::mutex> lk2(currentWriter_->mtx);
                // currentWriter_->fp->reset(nullptr);
                std::swap(currentWriter_, idleWriter_);
            }
            
            // 重置计数
            currentPacketCount_.store(0,  std::memory_order_relaxed); 
        }
        if (firstIframeNeed) continue;
        guard.release(); // 交给写线程释放
        // 分发到 currentWriter_
        {
            std::lock_guard<std::mutex> lk2(currentWriter_->mtx);
            currentWriter_->queue.push(meta);
        }
        currentWriter_->cv.notify_one();
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
    int tries = 200;
    while (--tries && running_) {
        bool ok = meta.core->tryGetEncodedPacket(meta);
        if (!ok) {
            std::this_thread::sleep_for(100us);
            continue;
        }
        out = meta.packet;
        if (out == nullptr) {
            // 安全检查
            fprintf(stderr, "[StreamWriter] obtainPacketForMeta: packet is nullptr, slot_id=%d\n", id);
            break;
        }
        return true;
    }
    if (tries <= 0) {
        fprintf(stderr, "[StreamWriter][slot_id:%d] Timeout, packet dropped\n", id);
    }
    meta.core->releaseSlot(id);
    return false;
}
