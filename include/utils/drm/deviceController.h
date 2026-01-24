/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-25 19:18:18
 * @FilePath: /include/utils/drm/deviceController.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef DEVICE_CONTROLLER_H
#define DEVICE_CONTROLLER_H
#include <system_error>
#include <memory>
#include <mutex>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>

#include "drm/drmBpp.h"
#include "fdWrapper.h"

class DeviceController;
using DrmDevicePtr = std::shared_ptr<DeviceController>;

namespace DrmDev {
    extern std::mutex fd_mutex;
    extern DrmDevicePtr fd_ptr;   // 全局唯一
}

static std::string fourccToString(uint32_t fourcc)
{
    char format[5];
    format[0] = static_cast<char>(fourcc & 0xFF);
    format[1] = static_cast<char>((fourcc >> 8) & 0xFF);
    format[2] = static_cast<char>((fourcc >> 16) & 0xFF);
    format[3] = static_cast<char>((fourcc >> 24) & 0xFF);
    format[4] = '\0';
    return std::string(format);
}

// 设备组合
struct drmModeDev {
    drmModeModeInfo mode;           // 显示模式
    uint16_t width;                 // 显示宽度
    uint16_t height;                // 显示高度
    uint32_t connector_id = 0;      // 连接器 ID
    uint32_t crtc_id = 0;           // CRTC ID
    drmModeCrtc* oldCrtc;           // 更改crtc前的配置

    ~drmModeDev(){
        if (oldCrtc){
            drmModeFreeCrtc(oldCrtc);
            oldCrtc = nullptr;
        }
    }
    drmModeDev() = default;
    // 移动
    drmModeDev(drmModeDev&& other)
        : mode(other.mode), width(other.width), height(other.height),
        connector_id(other.connector_id), crtc_id(other.crtc_id), oldCrtc(other.oldCrtc)
    {
        // 清空源对象, 避免重复释放
        other.oldCrtc = nullptr;
    }
    drmModeDev& operator=(drmModeDev&& other) {
        if (this != &other) {
            // 释放当前资源
            if (oldCrtc) {
                drmModeFreeCrtc(oldCrtc);
            }
            mode = other.mode;
            width = other.width;
            height = other.height;
            connector_id = other.connector_id;
            crtc_id = other.crtc_id;
            oldCrtc = std::exchange(other.oldCrtc, nullptr);
        }
        return *this;
    }

    // 禁用拷贝
    drmModeDev(const drmModeDev&) = delete;     
    drmModeDev& operator=(const drmModeDev&) = delete; 

};

// plane属性缓存
struct PlanesPropertyCache {
    int type;
    drmModePlanePtr plane;
    std::vector<uint32_t> formats;
    ~PlanesPropertyCache(){ 
        if (plane) drmModeFreePlane(plane); 
    }

    PlanesPropertyCache() : type(0), plane(nullptr) {}
    PlanesPropertyCache(PlanesPropertyCache&& other) noexcept
        : type(other.type), plane(other.plane),
        formats(std::move(other.formats)) { other.plane = nullptr; }

    PlanesPropertyCache& operator=(PlanesPropertyCache&& other){
        if (this != &other) {
            if (plane) drmModeFreePlane(plane);

            formats = std::move(other.formats);
            plane = std::exchange(other.plane, nullptr);

            type = other.type;
        }
        return *this;
    }

    // 禁用拷贝
    PlanesPropertyCache(const PlanesPropertyCache&) = delete;     
    PlanesPropertyCache& operator=(const PlanesPropertyCache&) = delete; 
};

using DevPtr = std::shared_ptr<drmModeDev>;
using SharedDev = std::vector<DevPtr>;
using PlanesCachePtr = std::shared_ptr<PlanesPropertyCache>;

// 全局唯一设备管理器
class DeviceController {
public:
    using ResourceCallback = std::function<void()>;
    // 资源获取
    static DrmDevicePtr create(const std::string& path = "/dev/dri/card0");
    // 资源释放
    ~DeviceController();
    
    // 禁用拷贝
    DeviceController(const DeviceController&) = delete;
    DeviceController& operator=(const DeviceController&) = delete;
    
    // 获取DRM设备fd
    int get() const { return fd_.get(); }
    
    // 注册资源回调
    void registerResourceCallback(
        const ResourceCallback& preRefreshCallback,
        const ResourceCallback& postRefreshCallback);

    // 获取属性ID
    uint32_t getPropertyId(int fd, drmModeObjectPropertiesPtr props, const char *name);

    // 获取设备组合
    SharedDev& getDevices() { 
        std::lock_guard<std::mutex> lock(devMutex_);
        return devices_; 
    }
    /* 获取指定类型的planeId
     * @param plane_type: DRM_PLANE_TYPE_OVERLAY , DRM_PLANE_TYPE_PRIMARY , DRM_PLANE_TYPE_CURSOR 
    */
   void getPossiblePlane(int plane_type, uint32_t format, std::vector<uint32_t>& out_ids);
    
    // 通过ID获取缓存的plane
    PlanesCachePtr getPlaneById(uint32_t id) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = planesCache_.find(id);
        return (it != planesCache_.end()) ? it->second : nullptr;
    }
    
    // 刷新资源
    std::shared_ptr<drmModeRes> refreshResources();
    SharedDev& refreshAllDevices();
    // 平面资源刷新
    size_t refreshPlane(uint32_t crtc_id);
    // 资源获取
    std::shared_ptr<drmModeRes> getResources() const { 
        std::lock_guard<std::mutex> lock(resMutex_);
        return resources_; 
    }
    std::shared_ptr<drmModePlaneRes> getPlaneResources() const { 
        std::lock_guard<std::mutex> lock(resMutex_);
        return planeResources_; 
    }
private:
    // 绑定connector和crtc
    int bindConn2Crtc(int fd, uint32_t conn_id, uint32_t crtc_id, drmModeModeInfo& mode);
    // 配置设备组合
    int setUpDevice(drmModeConnectorPtr connector, drmModeDev& dev);
    // 格式支持检查
    bool isFormatSupported(uint32_t format, drmModePlanePtr plane) const;
    int getPlaneType(uint32_t plane_id);
    explicit DeviceController(int fd);
    
    FdWrapper fd_;                                  // RAII 管理 drm fd

    mutable std::mutex crtcMutex_;
    std::unordered_map<uint32_t, bool> crtcStatu;   // CRTC 使用状态

    mutable std::mutex devMutex_;
    SharedDev devices_;                             // 设备组合列表(connector-encoder-crtc)

    // 资源指针
    mutable std::mutex resMutex_;
    std::shared_ptr<drmModeRes> resources_ = nullptr;
    std::shared_ptr<drmModePlaneRes> planeResources_ = nullptr;

    // plane 缓存
    mutable std::mutex cacheMutex_;
    std::unordered_map<uint32_t, PlanesCachePtr> planesCache_ = {};
    
    // 热插拔事件监听
    void handleHotplugEvent();
    
    // 资源刷新同步
    void notifyPreRefresh();
    void notifyPostRefresh();
    
    // 回调管理
    std::mutex callbackMutex_;
    std::vector<std::pair<ResourceCallback, ResourceCallback>> callbacks_;
};

#endif // DEVICE_CONTROLLER_H