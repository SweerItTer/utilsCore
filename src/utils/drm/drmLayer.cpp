/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-26 23:22:26
 * @FilePath: /EdgeVision/src/utils/drm/drmLayer.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "drm/drmLayer.h"
#include "rga/rga2drm.h"

DrmLayer::DrmLayer(std::vector<DmaBufferPtr> buffers, size_t cacheSize)
    : cacheSize_(cacheSize) 
{
    propertyGetters_ = {
        {"x",       [this]() { return PropertyValue(props_.srcX_); }},
        {"y",       [this]() { return PropertyValue(props_.srcY_); }},
        {"w",       [this]() { return PropertyValue(props_.srcwidth_); }},
        {"h",       [this]() { return PropertyValue(props_.srcheight_); }},
        {"crtcX",   [this]() { return PropertyValue(props_.crtcX_); }},
        {"crtcY",   [this]() { return PropertyValue(props_.crtcY_); }},
        {"crtcW",   [this]() { return PropertyValue(props_.crtcwidth_); }},
        {"crtcH",   [this]() { return PropertyValue(props_.crtcheight_); }},
        {"zOrder",  [this]() { return PropertyValue(props_.zOrder_); }},
        {"alpha",   [this]() { return PropertyValue(props_.alpha_); }},
        {"planeId", [this]() { return PropertyValue(props_.plane_id_); }},
        {"crtcId",  [this]() { return PropertyValue(props_.crtc_id_); }},
        {"fbId",    [this]() { return PropertyValue(props_.fb_id_); }},
        {"type",    [this]() { return PropertyValue(props_.type_); }}
    };
    propertySetters_ = {
        {"x",       [this](PropertyValue v) { props_.srcX_       = v.get<uint32_t>(); }},
        {"y",       [this](PropertyValue v) { props_.srcY_       = v.get<uint32_t>(); }},
        {"w",       [this](PropertyValue v) { props_.srcwidth_   = v.get<uint32_t>(); }},
        {"h",       [this](PropertyValue v) { props_.srcheight_  = v.get<uint32_t>(); }},
        {"crtcX",   [this](PropertyValue v) { props_.crtcX_      = v.get<uint32_t>(); }},
        {"crtcY",   [this](PropertyValue v) { props_.crtcY_      = v.get<uint32_t>(); }},
        {"crtcW",   [this](PropertyValue v) { props_.crtcwidth_  = v.get<uint32_t>(); }},
        {"crtcH",   [this](PropertyValue v) { props_.crtcheight_ = v.get<uint32_t>(); }},
        {"zOrder",  [this](PropertyValue v) { props_.zOrder_     = v.get<uint32_t>(); }},
        {"alpha",   [this](PropertyValue v) { props_.alpha_      = v.get<float>(); }},
        {"planeId", [this](PropertyValue v) { props_.plane_id_   = v.get<uint32_t>(); }},
        {"crtcId",  [this](PropertyValue v) { props_.crtc_id_    = v.get<uint32_t>(); }},
        {"fbId",    [this](PropertyValue v) { props_.fb_id_      = v.get<uint32_t>(); }},
        {"type",    [this](PropertyValue v) { props_.type_       = v.get<int>(); }}
    };
    updateBuffer(std::move(buffers));
}

DrmLayer::~DrmLayer()
{
    destroyFramebuffer();
}

void DrmLayer::updateBuffer(std::vector<DmaBufferPtr>&& buffers)
{
    if (buffers.empty() || buffers.size() > 4) {
        fprintf(stderr, "DrmLayer::DrmLayer: Invalid DmaBuffer count\n");
        return;
    }
    buffers_ = std::move(buffers); // 更新bufs
    uint32_t newFb = createFramebuffer();
    if (newFb == 0) {
        fprintf(stderr, "DrmLayer::updateBuffer: createFramebuffer failed\n");
        return;
    }
    
    // 更新 props_ 供回调使用
    props_.fb_id_ = newFb;
    if (updatelayer_){
        updatelayer_(shared_from_this(), props_.fb_id_);
    }
    // 记录新 FB 为最新 in-flight
    fbCache_.push_back(newFb);
}

uint32_t DrmLayer::createFramebuffer()
{
    uint32_t handles[4] = {0};
    uint32_t pitches[4] = {0};  // bytes per line
    uint32_t offsets[4] = {0};
    for (size_t i = 0; i < buffers_.size(); ++i) {
        auto buf = buffers_[i];
        if (nullptr == buf) {
            fprintf(stderr, "DrmLayer::createFramebuffer: Invalid DmaBuffer\n");
            return -1 ;
        }
        handles[i] = buf->handle();
        pitches[i] = buf->width();
        offsets[i] = buf->offset();
    }

    uint32_t format = buffers_[0]->format();
    uint32_t fbId = -1;
    std::lock_guard<std::mutex> lock(DrmDev::fd_mutex);
    // 创建 framebuffer
    int ret = drmModeAddFB2(
        DrmDev::fd_ptr->get(),  /* DRM 设备 fd */
        buffers_[0]->width(),   /* DmaBufferPtr 参数 */
        buffers_[0]->height(),
        format,
        handles,
        pitches,
        offsets,
        &fbId,
        0  // flags, 通常填 0
    );
    if (ret != 0 || fbId == (uint32_t)-1) {
        fprintf(stderr, "drmModeAddFB2 failed\tret: %d\n", ret);
    }
    return fbId;
}

void DrmLayer::onFenceSignaled()
{
    // fence 表示上一帧/更老的帧已经 scanout 完毕, 可以回收
    recycleOldFbs(/*keep=*/cacheSize_); // 只保留最新的 cacheSize_ 个
}

static int fbFree(uint32_t& fbId){
    if (fbId == 0 || fbId == (uint32_t)-1){
        return -1;
    }
    std::lock_guard<std::mutex> lock(DrmDev::fd_mutex);

    int ret = drmModeRmFB(DrmDev::fd_ptr->get(), fbId);
    if (ret < 0) {
        fprintf(stderr, "drmModeRmFB failed: %d\n", errno);
        return -1;
    } else {
        fbId = 0;
    }
    return 0;
}
  
// 销毁所有fb
void DrmLayer::destroyFramebuffer() {
    std::lock_guard<std::mutex> cacheLock(fbCacheMutex_);
    int ret = 0;
    size_t size = fbCache_.size();
    for(size_t i = 0; i < size; ++i){
        uint32_t fbId = fbCache_.front();
        fbCache_.pop_front();
        fbFree(fbId);
    }
}

// 回收旧fb
void DrmLayer::recycleOldFbs(size_t keep)
{
    std::lock_guard<std::mutex> cacheLock(fbCacheMutex_);
    // 确保至少保留 keep 个最新的 FB
    while (fbCache_.size() > keep) {
        uint32_t oldFb = fbCache_.front();
        int ret = fbFree(fbCache_.front());
        if (ret < 0) {
            // fprintf(stderr, "drmModeRmFB (recycle) failed: fb=%u errno=%d\n", oldFb, errno);
        }
        fbCache_.pop_front();
    }
}
