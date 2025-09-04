/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-25 23:32:28
 * @FilePath: /EdgeVision/include/utils/drm/planesCompositor.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef PLANES_COMPOSITOR_H  
#define PLANES_COMPOSITOR_H

#include <functional>
#include <vector>
#include <cstring>
#include <sys/poll.h>

#include "drm/deviceController.h"
#include "drm/drmLayer.h"

class PlanesCompositor{
    struct PlaneProperty
    {
        uint32_t property_crtc_id    = 0;
        uint32_t property_fb_id      = 0;
        uint32_t property_crtc_x     = 0;
        uint32_t property_crtc_y     = 0;
        uint32_t property_crtc_w     = 0;
        uint32_t property_crtc_h     = 0;
        uint32_t property_src_x      = 0;
        uint32_t property_src_y      = 0;
        uint32_t property_src_w      = 0;
        uint32_t property_src_h      = 0;
    };
    struct LayerProperty {
        uint32_t plane_id       = 0;
        uint32_t crtc_id        = 0;
        uint32_t fb_id          = 0;
        uint32_t crtc_x         = 0;
        uint32_t crtc_y         = 0;
        uint32_t crtc_w         = 0;
        uint32_t crtc_h         = 0;
        uint32_t src_x          = 0;
        uint32_t src_y          = 0;
        uint32_t src_w          = 0;
        uint32_t src_h          = 0;
        float    alpha          = 1.0f;
        int      type           = 0;
    };
    struct PropertyCache{
        PlaneProperty planeProperty; 
        LayerProperty layerProperty;
    };
public:
    static std::unique_ptr<PlanesCompositor> create() { return std::unique_ptr<PlanesCompositor>(new PlanesCompositor()); }
    // 添加图层
    void addLayer(const DrmLayerPtr& layer);
    // 移除图层
    void removeLayer(const DrmLayerPtr& layer);
    // 更新图层
    void updateLayer(const DrmLayerPtr& layer);
    void updateLayer(const DrmLayerPtr& layer, const uint32_t fb_id);
    // 提交合成
    int commit(int& fence);
    
    ~PlanesCompositor();
private:
    int updatePlaneProperty(bool firstFlag, const DrmLayerPtr& layer);
    void updateLayerCache(bool firstFlag, const DrmLayerPtr& layer);
    int addProperty2Req(const PropertyCache& propertyCache);
    
    PlanesCompositor();

    uint32_t out_fence_prop_id = -1;

    struct SharedPtrHash {
        size_t operator()(const DrmLayerPtr& p) const {
            return std::hash<DrmLayer*>()(p.get());
        }
    };
    
    struct SharedPtrEqual {
        bool operator()(const DrmLayerPtr& a, const DrmLayerPtr& b) const {
            return a.get() == b.get();
        }
    };
    /* 直接以 shared_ptr 作为键会存在多个地址不同的ptr实际指向的内存一致也会查找失败
     * 原因是 std::hash<std::shared_ptr> 特化实现是计算 shared_ptr 的地址而不是其指向的地址
     * 并且若 map 有大量的 shared_ptr 作为键,每一次增删改查都会调用std::hash
     * 同时内存开销也会变大(shared_ptr比裸指针多了许多成员)
     * 因此为了保证在 map 内有 shared_ptr 的拷贝(避免引用计数归零导致析构),又为了实现实际对象的匹配
     * 需要实现 SharedPtrHash,也就是自定义 hash 计算方法
     * 另外还需要提供比较器 SharedPtrEqual 来判断实际指向对象的内容或地址是否一致
     */
    std::unordered_map<DrmLayerPtr, PropertyCache, SharedPtrHash, SharedPtrEqual> layers_;
    
    drmModeAtomicReqPtr requires_;         // 提交结构体
    std::mutex layersMutex_;
};

using CompositorPtr = std::unique_ptr<PlanesCompositor>;

static void wait_fence(int fence_fd, std::function<void()> callback) {
    // fprintf(stdout, "Fence fd: %d\n", fence_fd);

    if (fence_fd < 0) return;

    struct pollfd pfd = {
        .fd = fence_fd,
        .events = POLLIN,
    };
    
    // 等待 fence 信号，超时时间 1000ms
    int poll_ret = poll(&pfd, 1, 1000);
    if (poll_ret < 0) {
        fprintf(stderr, "Poll on fence failed: %s\n", strerror(errno));
    } else if (poll_ret == 0) {
        fprintf(stderr, "Timeout waiting for fence\n");
    }
    close(fence_fd);
    callback();
}
#endif // PLANES_COMPOSITOR_H