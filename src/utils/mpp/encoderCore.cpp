#include "mpp/encoderCore.h"
#include <iostream>
#include <algorithm>

/* --------------------------------------------------------------------- */
/*                        缓冲区大小计算函数                             */
/* --------------------------------------------------------------------- */
static size_t calculateBufferSize(uint32_t width, uint32_t height) {
    // 计算YUV420SP(NV12)格式的原始帧大小
    size_t frame_size = width * height * 3 / 2;  // YUV420SP: Y + UV planes

    // 考虑编码后的压缩数据，通常压缩比为10:1到50:1
    // 对于H.264编码，为安全起见，我们分配原始帧大小的2倍
    size_t compressed_size = frame_size * 2;

    // 确保最小缓冲区大小为1MB，最大不超过16MB
    size_t min_buffer = 1 * 1024 * 1024;
    size_t max_buffer = 16 * 1024 * 1024;

    size_t buffer_size = std::max(min_buffer, std::min(compressed_size, max_buffer));

    // 对齐到4KB边界
    buffer_size = (buffer_size + 4095) & ~4095;

    fprintf(stdout, "[MppEncoderCore] Calculated buffer size: %zu bytes for %dx%d\n",
            buffer_size, width, height);

    return buffer_size;
}

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
    : core_id_(core_id) {
    resetConfig(cfg);
    worker_ = std::thread(&MppEncoderCore::workerThread, this);
    fprintf(stdout, "[MppEncoderCore:%d] Encoder core initialization successfully!\n", core_id_);
}

MppEncoderCore::~MppEncoderCore() {
    running_ = false;
    paused_ = false;
    switch_cv_.notify_all();           // 唤醒暂停的 worker
    pending_cv_.notify_all();          // 唤醒 wait 的 worker
    if (worker_.joinable())
        worker_.join();
    cleanupSlots();
}

void MppEncoderCore::contextInit(const MppEncoderContext::Config& cfg) {
    mpp_ctx_.reset( new MppEncoderContext(cfg) );
    if (nullptr == mpp_ctx_->ctx() || nullptr == mpp_ctx_->api() || nullptr == mpp_ctx_->encCfg()){
        fprintf(stderr, "[MppEncoderCore:%d] Encoder core initialization failed!\n", core_id_);
        return;
    }
}

void MppEncoderCore::resetConfig(const MppEncoderContext::Config& cfg){
    paused_ = true;
    std::lock_guard<std::mutex> lk(switch_mtx_);
    {
        std::lock_guard<std::mutex> lk1(pending_mtx_);
        std::lock_guard<std::mutex> lk2(free_mtx_);
        
        // 资源清理
        auto empty_pending = std::queue<int>();
        auto empty_free = std::queue<int>();
        pending_slots_.swap(empty_pending);
        free_slots_.swap(empty_free);
    }
    
    cleanupSlots();
    // 重置上下文
    contextInit(cfg);
    // 重新初始化 slot
    initSlots();
    paused_ = false;
    reload_need = true;
    switch_cv_.notify_all();
}

/* --------------------------------------------------------------------- */
/*                            Slot 内存池                                 */
/* --------------------------------------------------------------------- */
void MppEncoderCore::initSlots() {
    const auto& cfg = mpp_ctx_->getmCfg();

    uint32_t width  = cfg->prep_width;
    uint32_t height = cfg->prep_height;
    uint32_t drm_fmt = mpp2drm_format(cfg->prep_format);

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
        s.enc_buf.reset();
        s.packet.reset();
        s.dmabuf.reset();
        s.state.store(SlotState::invalid);
    }
}

/* --------------------------------------------------------------------- */
/*                            生产者接口                                  */
/* --------------------------------------------------------------------- */
std::pair<DmaBufferPtr, int> MppEncoderCore::acquireWritableSlot() {
    if (paused_) return {nullptr, -1};

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
        return {nullptr, -1};
    }
    return {slots_[slot_id].dmabuf, slot_id};
}

MppEncoderCore::EncodedMeta MppEncoderCore::submitFilledSlot(int slot_id) {
    if (paused_) return {};

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
        .core        = this
    };
}


MppEncoderCore::EncodedMeta MppEncoderCore::submitFilledSlotWithExternal(
    int slot_id, 
    DmaBufferPtr external_dma,
    std::shared_ptr<void> lifetime_holder)
{
    if (paused_) return {};

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
        .core        = this
    };
}

/* --------------------------------------------------------------------- */
/*                             编码线程                                   */
/* --------------------------------------------------------------------- */
void MppEncoderCore::workerThread() {
    MppCtx      ctx = mpp_ctx_->ctx();
    MppApi*     mpi = mpp_ctx_->api();
    // 开启硬件编码
    mpi->control(ctx, MPP_START, nullptr);
    auto fail_recover = [&](int slot_id){
        slots_[slot_id].state.store(SlotState::Writable);

        std::lock_guard<std::mutex> lk(free_mtx_);
        free_slots_.push(slot_id);
    };

    while (running_) {
        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (!running_) break;
            continue; // 轻量自旋, 避免内核阻塞
        }
        if (reload_need) {
            ctx = mpp_ctx_->ctx();
            mpi = mpp_ctx_->api();
            reload_need = false;
        }

        int slot_id = -1;
        // 阻塞等待有任务
        {
            std::unique_lock<std::mutex> lk(pending_mtx_);
            pending_cv_.wait(lk, [this]{ return !pending_slots_.empty() || !running_; });
            if (!running_) break;// 提前退出
            if (pending_slots_.empty()) continue;
            slot_id = pending_slots_.front();// 取出 slot_id
            pending_slots_.pop();
        }
        if (slot_id == -1) continue;
        
        auto& s = slots_[slot_id];
        
        // 置为 Encoding
        s.state.store(SlotState::Encoding);

        MppFrame   frame_raw   = nullptr;
        MppPacket  packet_raw  = nullptr;
        MppBuffer  buffer_raw  = nullptr;

        MppFrameGuard   frame_guard(&frame_raw);
        MppPacketGuard  packet_guard(&packet_raw);
        MppBufferGuard  buffer_guard(buffer_raw);

        // 创建可编码 frame
        if (!createEncodableFrame(s, frame_raw, buffer_raw)) {
            fprintf(stderr, "[MppEncoderCore:%d] createEncodableFrame failed.\n", core_id_);
            fail_recover(slot_id);
            continue;
        }

        // 拉取结果
        bool got = tryGetEncodedMppPacket(ctx, mpi, frame_raw, packet_raw);
        if (!got || !packet_raw) {
            fail_recover(slot_id);
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
            fprintf(stdout, "GET I(ntra) frame.\n");
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
    if (paused_) return false;

    // 非当前 core 或者 id 非法
    if (meta.core_id != core_id_ ||
        meta.slot_id < 0 ||
        static_cast<size_t>(meta.slot_id) >= SLOT_COUNT) {
        return false;
    }

    auto& s = slots_[meta.slot_id];
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

    auto& s = slots_[slot_id];
    if (s.state.load() == SlotState::invalid){
        fprintf(stderr, "[MppEncoderCore:%d] releaseSlot: slot %d state invalid\n", core_id_, slot_id);
        return;
    }
    if (s.using_external){
        s.external_dmabuf.reset();
        s.lifetime_holder.reset();
        s.using_external.store(false);
    }
    s.state.store(SlotState::Writable);

    std::lock_guard<std::mutex> lk(free_mtx_);
    free_slots_.push(slot_id);
}

/* --------------------------------------------------------------------- */
/*                               工厂函数                                */
/* --------------------------------------------------------------------- */
bool MppEncoderCore::createEncodableFrame(const MppEncoderCore::Slot& s, MppFrame& out_frame, MppBuffer& mpp_buf) {
    out_frame = nullptr;
    mpp_buf   = nullptr;
    if (!s.dmabuf || !s.enc_buf) return false; // 检查缓存的 buffer 是否有效

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
        import_info.fd   = s.external_dmabuf->fd();
        import_info.size = s.external_dmabuf->size();
        
        if (MPP_OK != mpp_buffer_import(&mpp_buf, &import_info)) {
            fprintf(stderr, "[MppEncoderCore:%d] mpp_buffer_import failed.\n", core_id_);
            return false;
        }
    }

    // 设置参数
    mpp_frame_set_width(out_frame,       s.dmabuf->width());
    mpp_frame_set_height(out_frame,      s.dmabuf->height());
    
    // Rockchip_Developer_Guide_MPP_CN.md: hor_stride	RK_U32	表示垂直方向相邻两行之间的距离，单位为byte数
    mpp_frame_set_hor_stride(out_frame,  s.dmabuf->pitch());
    mpp_frame_set_ver_stride(out_frame,  s.dmabuf->height());  // 像素单位
    mpp_frame_set_fmt(out_frame,         drm2mpp_format(s.dmabuf->format())); // 2022~2023 版本API
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
    const int max_polls = 200;

    for (int i = 0; i < max_polls && running_; ++i) {
        RK_S32 ret = mpi->encode_get_packet(ctx, &out_pkt);
        if (ret == MPP_OK && out_pkt) {
            return true;
        }
        if (ret != MPP_ERR_TIMEOUT) {
            fprintf(stderr, "[MppEncoderCore:%d] encode_get_packet error: %d\n", core_id_, ret);
            return false;
        }
        usleep(33);
    }
    fprintf(stderr, "[MppEncoderCore:%d] encode timeout\n", core_id_);
    return false;
}