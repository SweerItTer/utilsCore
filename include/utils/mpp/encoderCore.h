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
#include <chrono>
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
        uint64_t generation = 0;  ///< 该 meta 对应的 slot 代际, 用于 reset 后识别陈旧结果

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
        std::atomic<uint64_t> generation {0}; ///< slot 当前所属配置代际
    };
public:
    /**
     * @brief 构造函数
     * @param cfg 编码配置
     * @param core_id 核心编号
     */
    explicit MppEncoderCore(const MppEncoderContext::Config& cfg, int core_id);

    /**
     * @brief 使用新配置重建编码上下文和 slot 池
     * @param cfg 新的编码配置
     *
     * 该接口会等待当前正在进行的编码步骤安全退出, 然后再重建上下文与 slot。
     * 这样可以避免重置时 worker 线程仍在访问旧的 MPP 资源。
     */
    void resetConfig(const MppEncoderContext::Config& cfg);
    /**
     * @brief 析构函数
     */
    ~MppEncoderCore();
    
    /**
     * @brief 标记下一帧为编码结束帧
     * @note TODO(naming): 公共 API 名称沿用旧接口, 后续统一为 `markEndOfEncode()`
     */
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
     * @note TODO(naming): 仅按 `slot_id` 释放无法区分 reset 前后的陈旧 meta, 后续推荐使用 `releaseSlot(const EncodedMeta&)`
     */
    void releaseSlot(int slot_id);

    /**
     * @brief 根据 meta 中携带的代际信息释放 slot
     * @param meta 包含 slot 和 generation 的编码元信息
     *
     * 相比旧的 `releaseSlot(int)` 版本, 该接口可以在 reset 后安全忽略陈旧 meta, 避免把新一代
     * slot 错误地放回空闲队列。
     */
    void releaseSlot(const EncodedMeta& meta);

    /// 获取 core_id
    int coreId() const noexcept { return core_id_; }
    /// 获取负载(越小越多空间)
    size_t load() const noexcept { return SLOT_COUNT - free_slots_.size(); }

private:
    void workerThread(); ///< 编码线程主函数
    void contextInit(const MppEncoderContext::Config& cfg); ///< 初始化编码上下文
    void initSlots();    ///< 初始化 Slot 内存池
    void cleanupSlots(); ///< 清理 Slot 内存
    bool enterWorkerEncodeSection(int slot_id); ///< 在编码前与 reset 流程握手
    void leaveWorkerEncodeSection();            ///< 标记当前编码步骤结束
    void resetQueues();                         ///< 清空所有 slot 队列
    void recycleSlot(int slot_id);             ///< 将 slot 安全地回收到可写队列

    bool createEncodableFrame(const MppEncoderCore::Slot& s, MppFrame& out_frame, MppBuffer& mpp_buf);
    bool tryGetEncodedMppPacket(MppCtx ctx, MppApi* mpi, MppFrame frame_raw, MppPacket& out_pkt);
    int calculatePacketPollAttempts() const;   ///< 根据配置计算一次编码允许的轮询次数
    
    const int core_id_;              ///< 核心编号
    MppEncoderContext::Config currentConfig_{}; ///< 本地缓存配置, 供线程和等待策略复用
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

    /**
     * @brief 控制 reset 与 worker 编码步骤之间的握手
     *
     * `resetInProgress_` 为 true 时表示上下文和 slot 池正准备被替换; worker 必须在进入
     * 编码临界区前先观察这个标志。`workerEncoding_` 用于让 reset 等待当前编码步骤结束。
     */
    std::mutex control_mtx_;
    std::condition_variable control_cv_;
    bool resetInProgress_{false};
    bool workerEncoding_{false};
    uint64_t configGeneration_{1};
};

// SlotGuard 用于自动释放 Slot
class SlotGuard {
public:
    explicit SlotGuard(MppEncoderCore::EncodedMeta meta) : meta_(std::move(meta)) {}
    ~SlotGuard() { if (meta_.core != nullptr && meta_.slot_id != -1) meta_.core->releaseSlot(meta_); }
    void release() { meta_.core = nullptr; }
private:
    MppEncoderCore::EncodedMeta meta_{};
};

using SlotGuardPtr = std::shared_ptr<SlotGuard>;
using MppEncoderCorePtr = std::shared_ptr<MppEncoderCore>;
