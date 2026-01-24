/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-11-17 18:59:52
 * @FilePath: /include/utils/mpp/encoderCore.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include <atomic>
#include <thread>
#include <queue>
#include <array>
#include <memory>
#include <mutex>
#include <condition_variable>

#include <rockchip/rk_type.h>
#include <rockchip/mpp_err.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <unistd.h>
#include <cstring>

#include "dma/dmaBuffer.h"
#include "mpp/encoderContext.h"
#include "mpp/mppResourceGuard.h"
#include "mpp/formatTool.h"
#include "mpp/fileTools.h"

#include "types.h"

/* NV12(DMABUF / pointer) → MppFrame → MPP → MppPacket
 * 负责 Packet/Frame 内存池复用, MppFrame 构建  
 */
class MppEncoderCore {
public:
    /// Slot 数量, RK356X 实测值
    static constexpr size_t SLOT_COUNT = 15;

    /**
     * @class EncodedPacket
     * @brief 编码结果包封装
     */        
    class EncodedPacket {
    public:
        explicit EncodedPacket(MppPacket pkt, size_t len,  bool keyframe);
        ~EncodedPacket();
        MppPacket& rawPacket();
        void* data() const;
        size_t length() const;
        bool isKeyframe() const;
        u_int64_t getPts() const;
        
        void setPts(const std::chrono::steady_clock::time_point& tp);
        void setDataLen(size_t len);
        void setKeyframe(bool keyframe);
    private:
        uint64_t               pts_ = 0;   ///< 时间戳
        MppPacket              packet_ = nullptr; ///< MPP 编码输出包
        size_t                 data_len_ = 0; ///< 数据长度
        bool                   keyframe_ = false; ///< 是否关键帧
    };
    using EncodedPacketPtr = std::shared_ptr<EncodedPacket>;
    
    /**
     * @struct EncodedMeta
     * @brief 元信息
     */
    struct EncodedMeta {
        int      core_id    = -1; ///< EncoderCore 实例 id
        int      slot_id    = -1; ///< Slot 索引

        MppEncoderCore* core    = nullptr; ///< 所属 EncoderCore 指针
        EncodedPacketPtr packet = nullptr; ///< 编码结果包
    };

    /**
     * @enum SlotState
     * @brief Slot 状态
     */
    enum class SlotState : uint8_t {
        Writable = 0, ///< 可写
        Writing,      ///< 写入中
        Filled,       ///< 已填充等待编码
        Encoding,     ///< 正在编码
        Encoded,      ///< 编码完成
        invalid       ///< 无效状态
    };

    /**
     * @struct Slot
     * @brief 内部 Slot 数据结构
     */
    struct Slot {
        DmaBufferPtr           dmabuf;           ///< DMABUF 引用, 保持生命周期
        std::shared_ptr<MppBufferGuard> enc_buf; ///< 缓存导入后的 Buffer

        DmaBufferPtr external_dmabuf;            ///< 外部DMABUF 
        std::atomic_bool using_external{false};  ///< 使用外部DMABUF标志
        std::shared_ptr<void> lifetime_holder;   ///< 保留外部资源生命周期
        EncodedPacketPtr       packet = nullptr; ///< 编码结果
        std::atomic<SlotState> state {SlotState::invalid}; ///< 当前状态
    };
public:
    /**
     * @brief 构造函数
     * @param cfg 编码配置
     * @param core_id 核心编号
     */
    explicit MppEncoderCore(const MppEncoderContext::Config& cfg, int core_id);

    void resetConfig(const MppEncoderContext::Config& cfg);
    /**
     * @brief 析构函数
     */
    ~MppEncoderCore();
    
    void endOfthisEncode() { endOfEncode = true; }
    /**
     * @brief 获取可写 Slot 并置为 Writing
     * @return std::pair<DmaBufferPtr, int> 成功返回 {DMABUF, Slot 索引}, 失败返回 {nullptr, -1}
     */
    std::pair<DmaBufferPtr, int> acquireWritableSlot();

    /**
     * @brief 提交填充完成的 Slot, 置为 Filled
     * @param slot_id 填充完成的 Slot 索引
     * @param pts 时间戳
     * @return EncodedMeta 成功返回 meta 信息, 失败返回空 meta
     */
    EncodedMeta submitFilledSlot(int slot_id);
    EncodedMeta submitFilledSlotWithExternal(int slot_id, DmaBufferPtr external_dmabuf, std::shared_ptr<void> lifetime_holder);
    /**
     * @brief 获取 Encoded 的 Packet
     * @param meta 编码数据 meta
     * @param packet 输出参数, 存放编码结果
     * @return true 成功, false 失败
     */
    bool tryGetEncodedPacket(EncodedMeta& meta);

    /**
     * @brief 释放已使用的 Slot, 置为 Writable
     * @param slot_id 释放的 Slot 索引
     */
    void releaseSlot(int slot_id);

    /// 获取 core_id
    int coreId() const noexcept { return core_id_; }
    /// 获取负载(越小越多空间)
    size_t load() const noexcept { return SLOT_COUNT - free_slots_.size(); }

private:
    void workerThread(); ///< 编码线程主函数
    void contextInit(const MppEncoderContext::Config& cfg); ///< 初始化编码上下文
    void initSlots();    ///< 初始化 Slot 内存池
    void cleanupSlots(); ///< 清理 Slot 内存

    bool createEncodableFrame(const MppEncoderCore::Slot& s, MppFrame& out_frame, MppBuffer& mpp_buf);
    bool tryGetEncodedMppPacket(MppCtx ctx, MppApi* mpi, MppFrame frame_raw, MppPacket& out_pkt);
    
    const int core_id_;              ///< 核心编号
    std::unique_ptr<MppEncoderContext> mpp_ctx_;      ///< 编码上下文
    
    std::atomic_bool endOfEncode{false};

    std::array<Slot, SLOT_COUNT> slots_; ///< Slot 内存池
    std::queue<int> free_slots_;         ///< 可写 Slot 队列
    std::queue<int> pending_slots_;      ///< 待编码 Slot 队列

    std::thread worker_;                 ///< 编码线程
    std::atomic<bool> running_{true};    ///< 编码线程运行状态
    std::mutex              pending_mtx_;///< 用于编码线程等待或唤醒的条件变量(避免空转)
    std::mutex              free_mtx_;   ///< 保护 free_slots_ 队列的互斥锁
    std::condition_variable pending_cv_; ///< 保护 pending_slots_ 队列的互斥锁

    std::atomic<bool> paused_{false};    ///< 是否暂停编码
    std::atomic<bool> reload_need{false};///< 是否需要重载编码上下文
    std::mutex  switch_mtx_;             ///< 切换编码参数时的互斥锁
    std::condition_variable switch_cv_;  ///< 切换编码参数时的条件变量
};

// SlotGuard 用于自动释放 Slot
class SlotGuard {
public:
    SlotGuard(MppEncoderCore* c, int s) : core(c), slot_id(s) {}
    ~SlotGuard() { if (slot_id!=-1) core->releaseSlot(slot_id); }
    void release() { slot_id = -1; }
private:
    int slot_id = -1;
    MppEncoderCore* core;
};

using SlotGuardPtr = std::shared_ptr<SlotGuard>;
using MppEncoderCorePtr = std::shared_ptr<MppEncoderCore>;