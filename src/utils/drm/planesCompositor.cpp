/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-25 23:32:28
 * @FilePath: /EdgeVision/src/utils/drm/planesCompositor.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "drm/planesCompositor.h"
#include <sys/ioctl.h>

using namespace DrmDev;

PlanesCompositor::~PlanesCompositor() {
    removeAllLayer();
    if(requires_)
    {
        drmModeAtomicFree(requires_);
        requires_ = nullptr;
    }
}

PlanesCompositor::PlanesCompositor() {
    requires_ = drmModeAtomicAlloc();
}

bool PlanesCompositor::addLayer(const DrmLayerPtr& layer) {
    std::lock_guard<std::mutex> lock(layersMutex_);
    if (layers_.find(layer) != layers_.end()){
        fprintf(stderr, "[PlanesCompositor] Already to add layer, try use 'PlanesCompositor::updateLayer'.\n");
        return false;
    }
    // 更新图层状态缓存
    updateLayerCache(layer);
    // 更新 plane id / DRM 属性缓存
    updatePlaneProperty(layer);
    fprintf(stdout, "[PlanesCompositor] Add Layer.\n");
    return true;
}

bool PlanesCompositor::updatePropertyForLayer(const DrmLayerPtr& layer){
    if (layers_.find(layer) == layers_.end()){
        fprintf(stderr, "[PlanesCompositor] No layer can be update, cheak 'PlanesCompositor::addLayer'.\n");
        return false;
    }
    // 更新 plane id / DRM 属性缓存
    return !(updatePlaneProperty(layer) < 0);
}

bool PlanesCompositor::updateLayer(const DrmLayerPtr& layer) {
    std::lock_guard<std::mutex> lock(layersMutex_);
    if (layers_.find(layer) == layers_.end()){
        fprintf(stderr, "[PlanesCompositor] No layer can be update, cheak 'PlanesCompositor::addLayer'.\n");
        return false;
    }
    // 更新图层状态缓存
    updateLayerCache(layer);
    // fprintf(stdout, "[PlanesCompositor] Update Layer.\n");
    return true;
}

void PlanesCompositor::updateLayer(const DrmLayerPtr& layer, const uint32_t fb_id) {
    std::lock_guard<std::mutex> lock(layersMutex_);
    if (layers_.find(layer) == layers_.end())
        return;
    layers_[layer].layerProperty.fb_id = fb_id;
}

void PlanesCompositor::removeLayer(const DrmLayerPtr& layer) {
    std::lock_guard<std::mutex> lock(layersMutex_);
    layers_.erase(layer);
}

void PlanesCompositor::removeAllLayer(){
    std::lock_guard<std::mutex> lock(layersMutex_);
    layers_.clear();
}

int PlanesCompositor::commit(int& fence) {
    if (!requires_) {
        requires_ = drmModeAtomicAlloc(); // 避免出现未知释放
        if (!requires_) {
            fprintf(stderr, "Failed to allocate atomic request\n");
            return -ENOMEM;
        }
    }
    // 将游标移至开始, 覆写旧配置(逻辑清空)
    drmModeAtomicSetCursor(requires_, 0); 
    int ret = 0;
    uint32_t crtc_id = 0;
    {
    std::lock_guard<std::mutex> layersLock(layersMutex_);
    for (auto const& layer : layers_) {
        auto const& propertyCache = layer.second;
        crtc_id = propertyCache.layerProperty.crtc_id;
        if (propertyCache.layerProperty.fb_id == 0) {
            fprintf(stderr, "Layer fb_id is 0, skip this layer\n");
            continue;
        }
        // 添加有效layer到预配置
        int addRet = addProperty2Req(propertyCache);
        if (addRet < 0) {
            fprintf(stderr, "Failed to add properties for layer: %d\n", addRet);
            ret = addRet;
            break;
        }
    } // 解锁
    }
    std::lock_guard<std::mutex> fdLock(DrmDev::fd_mutex);

    uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_ATOMIC_NONBLOCK;
    
    fence = -1; // 存放 fence 的返回值
    if (out_fence_prop_id == -1) {
        fprintf(stderr, "Get inviled fence property id: -1\n");
        return -1;
    }
    ret = drmModeAtomicAddProperty(requires_, crtc_id, out_fence_prop_id, (uint64_t)&fence);
    if (ret < 0){
        fprintf(stderr, "Add CRTC fance property failed: %s\n", strerror(-ret));
    }
    ret = drmModeAtomicCommit(fd_ptr->get(), requires_, flags, &fence);
    if (ret < 0)
    {
        fprintf(stderr, "Atomic commit return non zero: %s\n", strerror(-ret));
    }
    return ret;
}

void PlanesCompositor::updateLayerCache(const DrmLayerPtr& layer) {
    PropertyCache& cache = layers_[layer]; // 引用, 直接修改 map

    // 复制所有属性值
    cache.layerProperty.plane_id = layer->getProperty("planeId").get<uint32_t>();
    cache.layerProperty.crtc_id  = layer->getProperty("crtcId").get<uint32_t>();
    cache.layerProperty.fb_id    = layer->getProperty("fbId").get<uint32_t>();
    cache.layerProperty.crtc_x   = layer->getProperty("crtcX").get<uint32_t>();
    cache.layerProperty.crtc_y   = layer->getProperty("crtcY").get<uint32_t>();
    cache.layerProperty.crtc_w   = layer->getProperty("crtcW").get<uint32_t>();
    cache.layerProperty.crtc_h   = layer->getProperty("crtcH").get<uint32_t>();
    cache.layerProperty.src_x    = layer->getProperty("x").get<uint32_t>();
    cache.layerProperty.src_y    = layer->getProperty("y").get<uint32_t>();
    cache.layerProperty.src_w    = layer->getProperty("w").get<uint32_t>();
    cache.layerProperty.src_h    = layer->getProperty("h").get<uint32_t>();
    cache.layerProperty.alpha    = layer->getProperty("alpha").get<float>();
    cache.layerProperty.type     = layer->getProperty("type").get<int>();
    cache.layerProperty.zpos     = layer->getProperty("zOrder").get<uint32_t>();

    // fprintf(stdout, "Update layer on plane:%u\n", cache.layerProperty.plane_id);
}

int PlanesCompositor::updatePlaneProperty(const DrmLayerPtr& layer) {
    PropertyCache& cache = layers_[layer]; // 引用, 直接修改 map
    // 不同驱动可能属性名称不同, zpos 可能是 "zpos" 或 "zposition"
    static std::vector<std::string> posName = {"zpos", "zposition"};
    static int posNameIndex = 0;
    // 用一个静态标记记录是否已经打印过
    static bool printedZPos = false;
    // 读取缓存
    auto planeId = cache.layerProperty.plane_id;
    auto crtcId = cache.layerProperty.crtc_id;

    std::lock_guard<std::mutex> fdLock(DrmDev::fd_mutex);
    int fd = DrmDev::fd_ptr->get();

    auto props = drmModeObjectGetProperties(fd, crtcId, DRM_MODE_OBJECT_CRTC);
    if (!props) {
        fprintf(stderr, "Failed to get crtc %u properties.\n", crtcId);
        return -1;
    }
    // 获取crtc使用buffer的fence属性ID(备份到成员变量)
    out_fence_prop_id = fd_ptr->getPropertyId(fd, props, "OUT_FENCE_PTR");
    drmModeFreeObjectProperties(props);

    props = drmModeObjectGetProperties(fd, planeId, DRM_MODE_OBJECT_PLANE);
    if (!props) {
        fprintf(stderr, "Failed to get plane %u properties.\n", planeId);
        return -1;
    }
    
    auto getProp = [&](const char* name) {
        return fd_ptr->getPropertyId(fd, props, name);
    };

    // 获取并缓存关键属性ID
    cache.planeProperty.property_crtc_id = getProp("CRTC_ID");
    cache.planeProperty.property_fb_id   = getProp("FB_ID");
    cache.planeProperty.property_crtc_x  = getProp("CRTC_X");
    cache.planeProperty.property_crtc_y  = getProp("CRTC_Y");
    cache.planeProperty.property_crtc_w  = getProp("CRTC_W");
    cache.planeProperty.property_crtc_h  = getProp("CRTC_H");
    cache.planeProperty.property_src_x   = getProp("SRC_X");
    cache.planeProperty.property_src_y   = getProp("SRC_Y");
    cache.planeProperty.property_src_w   = getProp("SRC_W");
    cache.planeProperty.property_src_h   = getProp("SRC_H");

    // 不同驱动可能属性名称不同, 先尝试 "zpos"
    for (int i = 0; i < posName.size(); i++, posNameIndex++) {
        int propertyId = getProp(posName[posNameIndex].c_str());
        if (propertyId == 0) continue; 
        cache.planeProperty.property_zpos = propertyId;
        break;
    }
    if (!printedZPos) {
        fprintf(stdout, "[PlanesCompositor] Plane %u use '%s' as zpos property id: %u\n",
                planeId, posName[posNameIndex].c_str(), cache.planeProperty.property_zpos);
        printedZPos = true;
    }

    drmModeFreeObjectProperties(props);
    return 0;
}

int PlanesCompositor::addProperty2Req(const PropertyCache& propertyCache) {
    int ret                   = 0;
    auto const& planeProperty = propertyCache.planeProperty;
    auto const& layerProperty = propertyCache.layerProperty;
    auto const& planeId       = layerProperty.plane_id;

    // 检查关键属性是否有效
    if (layerProperty.fb_id == 0) {
        fprintf(stderr, "Invalid FB_ID (0) for plane %u\n", planeId);
        return -EINVAL;
    }    
    if (layerProperty.crtc_id == 0) {
        fprintf(stderr, "Invalid CRTC_ID (0) for plane %u\n", planeId);
        return -EINVAL;
    }
    
    // 添加属性到请求
    ret |= drmModeAtomicAddProperty(requires_, planeId, planeProperty.property_crtc_id, layerProperty.crtc_id);
    ret |= drmModeAtomicAddProperty(requires_, planeId, planeProperty.property_fb_id,   layerProperty.fb_id);
    ret |= drmModeAtomicAddProperty(requires_, planeId, planeProperty.property_crtc_x,  layerProperty.crtc_x);
    ret |= drmModeAtomicAddProperty(requires_, planeId, planeProperty.property_crtc_y,  layerProperty.crtc_y);
    ret |= drmModeAtomicAddProperty(requires_, planeId, planeProperty.property_crtc_w,  layerProperty.crtc_w);
    ret |= drmModeAtomicAddProperty(requires_, planeId, planeProperty.property_crtc_h,  layerProperty.crtc_h);
    ret |= drmModeAtomicAddProperty(requires_, planeId, planeProperty.property_src_x,   layerProperty.src_x);
    ret |= drmModeAtomicAddProperty(requires_, planeId, planeProperty.property_src_y,   layerProperty.src_y);
    ret |= drmModeAtomicAddProperty(requires_, planeId, planeProperty.property_src_w,   layerProperty.src_w);
    ret |= drmModeAtomicAddProperty(requires_, planeId, planeProperty.property_src_h,   layerProperty.src_h);
    ret |= drmModeAtomicAddProperty(requires_, planeId, planeProperty.property_zpos,    layerProperty.zpos);
    if (ret < 0){
        fprintf(stderr, "Something error on add plane %u property: %s \n", planeId, strerror(-ret));
    }

    return ret;
}
