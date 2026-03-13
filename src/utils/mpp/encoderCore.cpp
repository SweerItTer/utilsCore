#include "mpp/encoderCore.h"
#include <iostream>
#include <algorithm>

// ------------------------ MppEncoderCore::EncodedPacket ------------------------ //
MppEncoderCore::EncodedPacket::EncodedPacket(MppPacket pkt, size_t len,  bool keyframe)
    : packet_(pkt), data_len_(len), keyframe_(keyframe) {}
MppEncoderCore::EncodedPacket::~EncodedPacket() {
    if (packet_) {
        mpp_packet_deinit(&packet_);
        packet_ = nullptr;
    }
}
MppPacket& MppEncoderCore::EncodedPacket::rawPacket() { return packet_; }
void* MppEncoderCore::EncodedPacket::data() const { if(packet_) return mpp_packet_get_data(packet_); else return nullptr; }
size_t MppEncoderCore::EncodedPacket::length() const { return data_len_; }
bool MppEncoderCore::EncodedPacket::isKeyframe() const { return keyframe_; }
u_int64_t MppEncoderCore::EncodedPacket::getPts() const { return pts_; }

void MppEncoderCore::EncodedPacket::setPts(const std::chrono::steady_clock::time_point& tp) {
    pts_ = std::chrono::duration_cast<std::chrono::microseconds>(
               tp.time_since_epoch()).count();
}
void MppEncoderCore::EncodedPacket::setDataLen(size_t len) { data_len_ = len; }
void MppEncoderCore::EncodedPacket::setKeyframe(bool keyframe) { keyframe_ = keyframe; }

// ------------------------ MppEncoderCore ------------------------ //
MppEncoderCore::MppEncoderCore(const MppEncoderContext::Config& cfg, int core_id)
    : core_id_(core_id),
      currentConfig_(cfg) {
    resetConfig(cfg);
    worker_ = std::thread(&MppEncoderCore::workerThread, this);
    fprintf(stdout, "[MppEncoderCore:%d] Encoder core initialization successfully!\n", core_id_);
}

MppEncoderCore::~MppEncoderCore() {
    running_ = false;
    {
        std::lock_guard<std::mutex> lock(control_mtx_);
        resetInProgress_ = false;
    }
    control_cv_.notify_all();
    pending_cv_.notify_all();
    if (worker_.joinable())
        worker_.join();
    cleanupSlots();
}

void MppEncoderCore::contextInit(const MppEncoderContext::Config& cfg) {
    currentConfig_ = cfg;
    mpp_ctx_.reset( new MppEncoderContext(cfg) );
    if (nullptr == mpp_ctx_->ctx() || nullptr == mpp_ctx_->api() || nullptr == mpp_ctx_->encCfg()){
        fprintf(stderr, "[MppEncoderCore:%d] Encoder core initialization failed!\n", core_id_);
        return;
    }
}

void MppEncoderCore::resetConfig(const MppEncoderContext::Config& cfg){
    {
        std::unique_lock<std::mutex> controlLock(control_mtx_);
        // reset 分两步:
        // 1. 提前拉起 resetInProgress_ 阻止新任务进入编码临界区
        // 2. 等待 workerEncoding_ 清零, 确保没有线程再持有旧 MPP 上下文或旧 slot buffer
        resetInProgress_ = true;
        pending_cv_.notify_all();
        control_cv_.wait(controlLock, [this] {
            return !workerEncoding_ || !running_;
        });
        ++configGeneration_;
    }

    resetQueues();
    cleanupSlots();
    contextInit(cfg);
    initSlots();
    {
        std::lock_guard<std::mutex> controlLock(control_mtx_);
        resetInProgress_ = false;
    }
    control_cv_.notify_all();
}

/* --------------------------------------------------------------------- */
/*                            Slot 内存池                                 */
/* --------------------------------------------------------------------- */
void MppEncoderCore::initSlots() {
    const auto* cfg = mpp_ctx_->getConfig();
    if (cfg == nullptr) {
        fprintf(stderr, "[MppEncoderCore:%d] initSlots skipped because config is null.\n", core_id_);
        return;
    }

    uint32_t width = static_cast<uint32_t>(cfg->prep_width);
    uint32_t height = static_cast<uint32_t>(cfg->prep_height);
    uint32_t drm_fmt = convertMppToDrmFormat(cfg->prep_format);

    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        auto dmabuf = DmaBuffer::create(
            width,
            height,
            drm_fmt,
            0, 0
        );

        if (!dmabuf) {
            fprintf(stderr, "[MppEncoderCore:%d] DmaBuffer create failed (slot %zu)\n", core_id_, i);
            continue;
        }

        // dmabuf 导入到 buffer
        MppBuffer mpp_buf = nullptr;
        MppBufferInfo import_info{};
        import_info.type = MPP_BUFFER_TYPE_EXT_DMA;
        import_info.fd   = dmabuf->fd();
        import_info.size = dmabuf->size();
        
        if (MPP_OK != mpp_buffer_import(&mpp_buf, &import_info)) {
            fprintf(stderr, "[MppEncoderCore:%d] mpp_buffer_import failed (slot %zu)\n", core_id_, i);
            continue;
        }

        // 伴随 Slot 的整个生命周期
        slots_[i].dmabuf = dmabuf;
        slots_[i].enc_buf = std::make_shared<MppBufferGuard>(mpp_buf); // 保存句柄
        slots_[i].packet = std::make_shared<EncodedPacket>(nullptr, 0, false);
        slots_[i].external_dmabuf.reset();
        slots_[i].using_external.store(false);
        slots_[i].lifetime_holder.reset();
        slots_[i].generation.store(configGeneration_);
        slots_[i].state  = SlotState::Writable;
        
        std::lock_guard<std::mutex> lk(free_mtx_);
        free_slots_.push(static_cast<int>(i));
    }

    fprintf(stdout, "[MppEncoderCore:%d] %d Slot init successfully.\n",
            core_id_, static_cast<int>(free_slots_.size()));
}

void MppEncoderCore::cleanupSlots()
{
    for (auto& s : slots_) {
        s.external_dmabuf.reset();
        s.using_external.store(false);
        s.lifetime_holder.reset();
        s.enc_buf.reset();
        s.packet.reset();
        s.dmabuf.reset();
        s.state.store(SlotState::invalid);
    }
}

void MppEncoderCore::resetQueues() {
    {
        std::lock_guard<std::mutex> pendingLock(pending_mtx_);
        std::queue<int> emptyPending;
        pending_slots_.swap(emptyPending);
    }
    {
        std::lock_guard<std::mutex> freeLock(free_mtx_);
        std::queue<int> emptyFree;
        free_slots_.swap(emptyFree);
    }
}

/* --------------------------------------------------------------------- */
/*                            生产者接口                                  */
/* --------------------------------------------------------------------- */
std::pair<DmaBufferPtr, int> MppEncoderCore::acquireWritableSlot() {
    {
        std::lock_guard<std::mutex> controlLock(control_mtx_);
        if (resetInProgress_ || !running_) {
            return {nullptr, -1};
        }
    }

    int slot_id = -1;    
    {
        std::lock_guard<std::mutex> lk(free_mtx_);
        // 无可用 slot
        if (free_slots_.empty()) {
            return {nullptr, -1};
        }
        slot_id = free_slots_.front();
        free_slots_.pop();
    }
    // 置为 Writing 防止被其他线程抢走
    auto expected = SlotState::Writable;
    if (!slots_[slot_id].state.compare_exchange_weak(expected, SlotState::Writing)){
        // 状态不正确
        fprintf(stderr, "[MppEncoderCore:%d] acquireWritableSlot: slot %d state invalid\n", core_id_, slot_id);
        recycleSlot(slot_id);
        return {nullptr, -1};
    }
    return {slots_[slot_id].dmabuf, slot_id};
}

MppEncoderCore::EncodedMeta MppEncoderCore::submitFilledSlot(int slot_id) {
    {
        std::lock_guard<std::mutex> controlLock(control_mtx_);
        if (resetInProgress_ || !running_) {
            return {};
        }
    }

    if (slot_id < 0 || static_cast<size_t>(slot_id) >= SLOT_COUNT) {
        // 无效 slot_id
        return {};
    }

    auto& s = slots_[slot_id];

    // 置为 Filled
    auto expected = SlotState::Writing;
    if (!s.state.compare_exchange_weak(expected, SlotState::Filled)){
        // 状态不正确
        fprintf(stderr, "[MppEncoderCore:%d] submitFilledSlot: slot %d state invalid\n", core_id_, slot_id);
        return {};
    }
    s.packet->setPts(std::chrono::steady_clock::now());    // 更新时间戳
    // 加入待编码队列
    {
        std::lock_guard<std::mutex> lk(pending_mtx_);
        pending_slots_.push(slot_id);
    }
    pending_cv_.notify_one();   // 立即唤醒 worker
    // 立即返回 meta (data_length/keyframe 解码完成时填充)
    return EncodedMeta{
        .core_id     = core_id_,
        .slot_id     = slot_id,
        .generation  = s.generation.load(),
        .core        = this
    };
}


MppEncoderCore::EncodedMeta MppEncoderCore::submitFilledSlotWithExternal(
    int slot_id, 
    DmaBufferPtr external_dma,
    std::shared_ptr<void> lifetime_holder)
{
    {
        std::lock_guard<std::mutex> controlLock(control_mtx_);
        if (resetInProgress_ || !running_) {
            return {};
        }
    }

    if (slot_id < 0 || static_cast<size_t>(slot_id) >= SLOT_COUNT) {
        // 无效 slot_id
        return {};
    }
    if (!external_dma) {
        fprintf(stderr, "[MppEncoderCore:%d] submitFilledSlotWithExternal received null dmabuf.\n", core_id_);
        return {};
    }

    auto& s = slots_[slot_id];

    // 置为 Filled
    auto expected = SlotState::Writing;
    if (!s.state.compare_exchange_weak(expected, SlotState::Filled)){
        // 状态不正确
        fprintf(stderr, "[MppEncoderCore:%d] submitFilledSlot: slot %d state invalid\n", core_id_, slot_id);
        return {};
    }
    s.external_dmabuf = external_dma;
    s.using_external = true;
    s.lifetime_holder = lifetime_holder;
    s.packet->setPts(std::chrono::steady_clock::now());    // 更新时间戳
    {
        std::lock_guard<std::mutex> lk(pending_mtx_);
        pending_slots_.push(slot_id);
    }
    pending_cv_.notify_one();   // 立即唤醒 worker
    // 立即返回 meta (data_length/keyframe 解码完成时填充)
    return EncodedMeta{
        .core_id     = core_id_,
        .slot_id     = slot_id,
        .generation  = s.generation.load(),
        .core        = this
    };
}

/* --------------------------------------------------------------------- */
/*                             编码线程                                   */
/* --------------------------------------------------------------------- */
void MppEncoderCore::workerThread() {
    uint64_t startedGeneration = 0;
    while (running_) {
        {
            std::unique_lock<std::mutex> controlLock(control_mtx_);
            control_cv_.wait(controlLock, [this] {
                return !resetInProgress_ || !running_;
            });
            if (!running_) {
                break;
            }
        }

        int slot_id = -1;
        // 阻塞等待有任务
        {
            std::unique_lock<std::mutex> lk(pending_mtx_);
            pending_cv_.wait(lk, [this] {
                return !pending_slots_.empty() || !running_ || resetInProgress_;
            });
            if (!running_) break;// 提前退出
            if (resetInProgress_) continue;
            if (pending_slots_.empty()) continue;
            slot_id = pending_slots_.front();// 取出 slot_id
            pending_slots_.pop();
        }
        if (slot_id == -1) continue;
        if (!enterWorkerEncodeSection(slot_id)) {
            recycleSlot(slot_id);
            continue;
        }

        auto& s = slots_[slot_id];

        s.state.store(SlotState::Encoding);

        MppFrame   frame_raw   = nullptr;
        MppPacket  packet_raw  = nullptr;
        MppBuffer  buffer_raw  = nullptr;

        MppFrameGuard   frame_guard(&frame_raw);
        MppPacketGuard  packet_guard(&packet_raw);
        MppBufferGuard  buffer_guard(buffer_raw);
        auto leaveScope = std::shared_ptr<void>(nullptr, [this](void*) { leaveWorkerEncodeSection(); });
        (void)leaveScope;

        MppCtx ctx = mpp_ctx_ ? mpp_ctx_->ctx() : nullptr;
        MppApi* mpi = mpp_ctx_ ? mpp_ctx_->api() : nullptr;
        uint64_t currentGeneration = 0;
        {
            std::lock_guard<std::mutex> controlLock(control_mtx_);
            currentGeneration = configGeneration_;
        }
        if (ctx == nullptr || mpi == nullptr) {
            fprintf(stderr, "[MppEncoderCore:%d] workerThread found invalid MPP context.\n", core_id_);
            recycleSlot(slot_id);
            continue;
        }
        if (startedGeneration != currentGeneration &&
            MPP_OK != mpi->control(ctx, MPP_START, nullptr)) {
            fprintf(stderr, "[MppEncoderCore:%d] failed to start encoder.\n", core_id_);
            recycleSlot(slot_id);
            continue;
        }
        startedGeneration = currentGeneration;

        // 创建可编码 frame
        if (!createEncodableFrame(s, frame_raw, buffer_raw)) {
            fprintf(stderr, "[MppEncoderCore:%d] createEncodableFrame failed.\n", core_id_);
            recycleSlot(slot_id);
            continue;
        }

        // 拉取结果
        bool got = tryGetEncodedMppPacket(ctx, mpi, frame_raw, packet_raw);
        if (!got || !packet_raw) {
            recycleSlot(slot_id);
            fprintf(stderr, "[MppEncoderCore:%d] encode_get_packet timeout or error\n", core_id_);
            continue;
        }
    
        /* packet_raw 实际生命周期由mpp内部管理, 获取到的只是只读的指针
         * 如果尝试保存的只是指针, 实际内存依旧会被mpp修改, 导致无效或者脏数据
         * 因此只能使用memcp深拷贝数据...
        */
        MppPacket& rawpkt = s.packet->rawPacket();
        mpp_packet_deinit(&rawpkt);
        mpp_packet_copy_init(&rawpkt, packet_raw);
        size_t len = mpp_packet_get_length(packet_raw);
        
        // 更新 EncodedPacket 信息
        s.packet->setDataLen(len);

        RK_S32 intra_flag = 0;
        if (mpp_packet_has_meta(packet_raw)){
            MppMeta meta = mpp_packet_get_meta(packet_raw);
            mpp_meta_get_s32(meta, KEY_OUTPUT_INTRA, &intra_flag);
        }
        // 关键帧判断
        if (intra_flag == 0){
            s.packet->setKeyframe(false);
        } else{
            s.packet->setKeyframe( true );
        }
        s.state.store(SlotState::Encoded);
        // Guard 自动释放资源
    }
}

/* --------------------------------------------------------------------- */
/*                             写线程接口                                 */
/* --------------------------------------------------------------------- */
bool MppEncoderCore::tryGetEncodedPacket(EncodedMeta& meta) {
    {
        std::lock_guard<std::mutex> controlLock(control_mtx_);
        if (resetInProgress_) {
            return false;
        }
    }

    // 非当前 core 或者 id 非法
    if (meta.core_id != core_id_ ||
        meta.slot_id < 0 ||
        static_cast<size_t>(meta.slot_id) >= SLOT_COUNT) {
        return false;
    }

    auto& s = slots_[meta.slot_id];
    if (meta.generation != s.generation.load()) {
        fprintf(stderr, "[MppEncoderCore:%d] tryGetEncodedPacket ignored stale meta for slot %d.\n",
                core_id_, meta.slot_id);
        return false;
    }
    // 检查状态
    if (s.state.load() != SlotState::Encoded) {
        return false;
    }
    // 返回结果
    meta.packet   = s.packet;
    // 释放引用(由meta析构)
    s.packet.reset(new EncodedPacket(nullptr, 0, false));
    return true;
}

void MppEncoderCore::releaseSlot(int slot_id) {
    if (slot_id < 0 || static_cast<size_t>(slot_id) >= SLOT_COUNT){
        // 无效 slot_id
        fprintf(stderr, "[MppEncoderCore:%d] releaseSlot: invalid slot_id %d\n", core_id_, slot_id);
        return;
    }

    recycleSlot(slot_id);
}

void MppEncoderCore::releaseSlot(const EncodedMeta& meta) {
    if (meta.slot_id < 0 || static_cast<size_t>(meta.slot_id) >= SLOT_COUNT) {
        return;
    }
    const uint64_t slotGeneration = slots_[meta.slot_id].generation.load();
    if (meta.generation != 0 && meta.generation != slotGeneration) {
        fprintf(stderr,
                "[MppEncoderCore:%d] releaseSlot ignored stale meta for slot %d (meta=%llu slot=%llu).\n",
                core_id_,
                meta.slot_id,
                static_cast<unsigned long long>(meta.generation),
                static_cast<unsigned long long>(slotGeneration));
        return;
    }
    recycleSlot(meta.slot_id);
}

/* --------------------------------------------------------------------- */
/*                               工厂函数                                */
/* --------------------------------------------------------------------- */
bool MppEncoderCore::createEncodableFrame(const MppEncoderCore::Slot& s, MppFrame& out_frame, MppBuffer& mpp_buf) {
    out_frame = nullptr;
    mpp_buf   = nullptr;
    const DmaBufferPtr sourceDmabuf = s.using_external ? s.external_dmabuf : s.dmabuf;
    if (!sourceDmabuf) {
        fprintf(stderr, "[MppEncoderCore:%d] source dmabuf is null.\n", core_id_);
        return false;
    }
    if (!s.using_external && !s.enc_buf) {
        fprintf(stderr, "[MppEncoderCore:%d] internal slot buffer is null.\n", core_id_);
        return false;
    }

    // ============ 零拷贝导入 dmabuf fd ============
    // 复用 Slot 缓存的 buffer
    
    // 创建 frame
    RK_S32 ret = mpp_frame_init(&out_frame);
    if (ret != MPP_OK || !out_frame) {
        fprintf(stderr, "[MppEncoderCore:%d] mpp_frame_init failed\n", core_id_);
        return false;
    }
    if (s.using_external){
        // dmabuf 导入到 buffer
        MppBufferInfo import_info{};
        import_info.type = MPP_BUFFER_TYPE_EXT_DMA;
        import_info.fd   = sourceDmabuf->fd();
        import_info.size = sourceDmabuf->size();
        
        if (MPP_OK != mpp_buffer_import(&mpp_buf, &import_info)) {
            fprintf(stderr, "[MppEncoderCore:%d] mpp_buffer_import failed.\n", core_id_);
            return false;
        }
    }

    // 设置参数
    mpp_frame_set_width(out_frame,       sourceDmabuf->width());
    mpp_frame_set_height(out_frame,      sourceDmabuf->height());
    
    // Rockchip_Developer_Guide_MPP_CN.md: hor_stride	RK_U32	表示垂直方向相邻两行之间的距离, 单位为byte数
    mpp_frame_set_hor_stride(out_frame,  sourceDmabuf->pitch());
    mpp_frame_set_ver_stride(out_frame,  sourceDmabuf->height());  // 像素单位
    mpp_frame_set_fmt(out_frame,         convertDrmToMppFormat(sourceDmabuf->format())); // 2022~2023 版本API
    // mpp_frame_set_buffer 会增加引用计数, mpp_frame_deinit 会减少引用计数
    // s.enc_buf 本身的引用计数(1)保持不变
    mpp_frame_set_buffer(out_frame,      s.using_external ? mpp_buf : s.enc_buf->get());
    mpp_frame_set_pts(out_frame,         s.packet->getPts());
    return true;
}

bool MppEncoderCore::tryGetEncodedMppPacket(MppCtx ctx, MppApi* mpi, MppFrame frame_raw, MppPacket& out_pkt) {
    if (true == endOfEncode) {
        mpp_frame_set_eos(frame_raw, true);
    }
    if (!running_){
        fprintf(stderr, "[MppEncoderCore:%d] Core has been shutdown.\n", core_id_);
        return false;
    }
    // 提交编码
    if (mpi->encode_put_frame(ctx, frame_raw) != MPP_OK) {
        fprintf(stderr, "[MppEncoderCore:%d] encode_put_frame error\n", core_id_);
        return false;
    }
    out_pkt = nullptr;
    const int maxPolls = calculatePacketPollAttempts();
    const auto pollInterval = std::chrono::microseconds(currentConfig_.packet_poll_interval_us);

    for (int i = 0; i < maxPolls && running_; ++i) {
        {
            std::lock_guard<std::mutex> controlLock(control_mtx_);
            if (resetInProgress_) {
                fprintf(stderr, "[MppEncoderCore:%d] packet polling interrupted by reset.\n", core_id_);
                return false;
            }
        }
        RK_S32 ret = mpi->encode_get_packet(ctx, &out_pkt);
        if (ret == MPP_OK && out_pkt) {
            return true;
        }
        if (ret != MPP_ERR_TIMEOUT) {
            fprintf(stderr, "[MppEncoderCore:%d] encode_get_packet error: %d\n", core_id_, ret);
            return false;
        }
        std::this_thread::sleep_for(pollInterval);
    }
    fprintf(stderr, "[MppEncoderCore:%d] encode timeout\n", core_id_);
    return false;
}

bool MppEncoderCore::enterWorkerEncodeSection(int slot_id) {
    std::unique_lock<std::mutex> controlLock(control_mtx_);
    if (!running_) {
        return false;
    }
    // worker 在真正触碰 MPP ctx 前必须再次检查 reset 标志。
    // 这样即使 slot 已经从 pending 队列中弹出, 也不会在 reset 过程中继续使用旧资源。
    if (resetInProgress_) {
        fprintf(stderr, "[MppEncoderCore:%d] reset in progress, recycle slot %d before encode.\n", core_id_, slot_id);
        return false;
    }
    workerEncoding_ = true;
    return true;
}

void MppEncoderCore::leaveWorkerEncodeSection() {
    std::lock_guard<std::mutex> controlLock(control_mtx_);
    workerEncoding_ = false;
    control_cv_.notify_all();
}

void MppEncoderCore::recycleSlot(int slot_id) {
    auto& slot = slots_[slot_id];
    SlotState state = slot.state.load();
    while (state != SlotState::Writable) {
        if (state == SlotState::invalid) {
            fprintf(stderr, "[MppEncoderCore:%d] recycleSlot ignored invalid slot %d.\n", core_id_, slot_id);
            return;
        }
        // compare_exchange 让 releaseSlot 变成幂等操作:
        // 只有第一个把 slot 状态切回 Writable 的线程会重新入 free_queue, 后续重复释放只会看到 Writable/invalid。
        if (slot.state.compare_exchange_weak(state, SlotState::Writable)) {
            if (slot.using_external.exchange(false)) {
                slot.external_dmabuf.reset();
                slot.lifetime_holder.reset();
            }
            std::lock_guard<std::mutex> freeLock(free_mtx_);
            free_slots_.push(slot_id);
            return;
        }
    }
}

int MppEncoderCore::calculatePacketPollAttempts() const {
    const int intervalUs = std::max(1, currentConfig_.packet_poll_interval_us);
    const int retriesByTimeout = std::max(1, currentConfig_.packet_ready_timeout_us / intervalUs);
    return std::max(currentConfig_.packet_poll_retries, retriesByTimeout);
}
