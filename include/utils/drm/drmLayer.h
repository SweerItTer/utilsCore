/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-26 23:18:51
 * @FilePath: /EdgeVision/include/utils/drm/drmLayer.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */

#ifndef DRM_LAYER_H
#define DRM_LAYER_H

#include <cstdint>
#include <memory>
#include <deque>
#include <unordered_map>
#include <functional>

#include "simpleVariant.h"
#include "dma/dmaBuffer.h"

class DrmLayer : public std::enable_shared_from_this<DrmLayer> {
public:
    enum class planeType {
        overlay = 0,
        primary = 1,
        cursor  = 2
    };
    struct LayerProperties
    {
        int      type_       = 0;    // 图层类型, 对应 "type" (0=overlay, 1=primary, 2=cursor)
        uint32_t plane_id_   = -1;   // DRM plane ID, 对应 "planeId"
        uint32_t crtc_id_    = -1;   // 绑定的 CRTC ID, 对应 "crtcId"
        uint32_t fb_id_      = 0;    // 绑定的 framebuffer ID, 对应 "fbId"

        // 源图像区域
        uint32_t srcX_       = 0;    // 源图像 X 坐标, 对应 "x"
        uint32_t srcY_       = 0;    // 源图像 Y 坐标, 对应 "y"
        uint32_t srcwidth_   = 0;    // 源图像宽度, 对应 "w"
        uint32_t srcheight_  = 0;    // 源图像高度, 对应 "h"

        // 显示区域 (CRTC 空间)
        uint32_t crtcX_      = 0;    // 显示区域 X 坐标, 对应 "crtcX"
        uint32_t crtcY_      = 0;    // 显示区域 Y 坐标, 对应 "crtcY"
        uint32_t crtcwidth_  = 0;    // 显示区域宽度, 对应 "crtcW"
        uint32_t crtcheight_ = 0;    // 显示区域高度, 对应 "crtcH"

        uint32_t zOrder_     = 0;    // 图层 Z 顺序, 对应 "zOrder"
        float    alpha_      = 1.0f; // 透明度, 对应 "alpha"
    };

    using PropertyValue = SimpleVariant<int, uint32_t, float>;

    using updateLayerCallback = std::function<void(const std::shared_ptr<DrmLayer>&, uint32_t)>;

    explicit DrmLayer(std::vector<DmaBufferPtr> buffers, size_t cacheSize);
    DrmLayer() = default;
    ~DrmLayer();

    void setUpdateCallback(updateLayerCallback updatelayer){
        updatelayer_ = updatelayer;
    }
    
    // 设置图层属性
    void setProperty(const LayerProperties& props){
        props_ = props;
    }
    /* 设置指定属性
     * setProperty("width", static_cast<uint32_t>(1920));
     */
    void setProperty(const std::string &name, PropertyValue value) {
        auto it = propertySetters_.find(name);
        if (propertySetters_.end() != it) {
            it->second(value);
            return;
        }
        throw std::invalid_argument("Unknown property: " + name);
    }
    
    /* 获取指定属性
     * auto width = std::get<uint32_t>(layer.getProperty("width"));
     */
    PropertyValue getProperty(const std::string &name) const {
        auto it = propertyGetters_.find(name);
        if (propertyGetters_.end() != it) {
            return it->second();
        }
        throw std::invalid_argument("Unknown property: " + name);
    }

    
    // 更新缓冲区
    void updateBuffer(std::vector<DmaBufferPtr> buffers);
    // 等待fence更新fb(采用多级缓冲避免直接销毁fb)
    void onFenceSignaled();
private:
    // 导入 Dmabuf 创建 fb
    uint32_t createFramebuffer();
    // 销毁 fb 缓存队列
    void destroyFramebuffer();
    // 回收旧 fb, 使得队列中至多保留 keep 个
    void recycleOldFbs(size_t keep = 1);

    LayerProperties props_{};
    updateLayerCallback updatelayer_;
    std::vector<DmaBufferPtr> buffers_{};

    // fb 缓存队列
    size_t cacheSize_;
    std::deque<uint32_t> fbCache_;
    mutable std::mutex fbCacheMutex_;

    std::unordered_map<std::string, std::function<void(PropertyValue)>> propertySetters_;   
    std::unordered_map<std::string, std::function<PropertyValue()>> propertyGetters_;
};
    
using DrmLayerPtr = std::shared_ptr<DrmLayer>;

#endif // DRM_LAYER_H