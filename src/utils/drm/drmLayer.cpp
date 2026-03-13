/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-26 23:22:26
 * @FilePath: /src/utils/drm/drmLayer.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "drm/drmLayer.h"
#include "drm/deviceController.h"
#include "rga/formatTool.h"

DrmLayer::DrmLayer(std::vector<DmaBufferPtr> buffers, size_t cacheSize)
    : framebufferCache_(DrmDev::fd_ptr ? DrmDev::fd_ptr->getFramebufferCache() : nullptr)
    , cacheSize_(cacheSize) 
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

DrmLayer::~DrmLayer() {
    releaseQueuedFramebuffers();
}

void DrmLayer::updateBuffer(std::vector<DmaBufferPtr> buffers) {
    if (buffers.empty() || buffers.size() > 4) {
        fprintf(stderr, "DrmLayer::DrmLayer: Invalid DmaBuffer count\n");
        return;
    }
    buffers_ = std::move(buffers); // 更新bufs
    const auto framebufferHandle = acquireFramebufferHandle();
    if (!framebufferHandle.valid()) {
        fprintf(stderr, "DrmLayer::updateBuffer: acquireFramebufferHandle failed\n");
        return;
    }
    
    // 更新 props_ 供回调使用
    props_.fb_id_ = framebufferHandle.framebufferId;
    if (updatelayer_){
        updatelayer_(shared_from_this(), props_.fb_id_);
    }
    // 记录新 framebuffer 句柄为最新 in-flight.
    {
        std::lock_guard<std::mutex> lock(framebufferMutex_);
        framebufferHandles_.push_back(framebufferHandle);
    }
}

FramebufferCache::FramebufferHandle DrmLayer::acquireFramebufferHandle() const {
    if (!framebufferCache_ || buffers_.empty() || !buffers_[0] || !DrmDev::fd_ptr) {
        return {};
    }

    return framebufferCache_->acquireFramebuffer(
        buffers_,
        buffers_[0]->width(),
        buffers_[0]->height(),
        buffers_[0]->format(),
        DrmDev::fd_ptr->currentGeneration());
}

void DrmLayer::onFenceSignaled() {
    // fence 表示上一帧/更老的帧已经 scanout 完毕, 可以回收旧句柄.
    recycleQueuedFramebuffers(/*keep=*/cacheSize_);
}

void DrmLayer::releaseQueuedFramebuffers() {
    std::lock_guard<std::mutex> lock(framebufferMutex_);
    if (!framebufferCache_) {
        framebufferHandles_.clear();
        return;
    }
    while (!framebufferHandles_.empty()) {
        framebufferCache_->releaseFramebuffer(framebufferHandles_.front());
        framebufferHandles_.pop_front();
    }
}

void DrmLayer::recycleQueuedFramebuffers(size_t keep) {
    std::lock_guard<std::mutex> lock(framebufferMutex_);
    if (!framebufferCache_) {
        framebufferHandles_.clear();
        return;
    }
    while (framebufferHandles_.size() > keep) {
        framebufferCache_->releaseFramebuffer(framebufferHandles_.front());
        framebufferHandles_.pop_front();
    }
}
