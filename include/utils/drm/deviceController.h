/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-25 19:18:18
 * @FilePath: /EdgeVision/include/utils/drm/deviceController.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef DEVICE_CONTROLLER_H
#define DEVICE_CONTROLLER_H
#include <system_error>
#include <memory>
#include <mutex>
#include <vector>

#include "drm/drmBpp.h"
#include "fdWrapper.h"

class DeviceController;
using DrmDevicePtr = std::shared_ptr<DeviceController>;
  
namespace DrmDev {
    extern std::mutex fd_mutex;     // 设备全局锁
    extern DrmDevicePtr fd_ptr;     // 内部全局唯一 drm_fd
}

class DeviceController {
public:
    static std::shared_ptr<DeviceController> create(const std::string& path = "/dev/dri/card0");
    ~DeviceController(){}
    
    // 禁用拷贝
    DeviceController(const DeviceController&) = delete;
    DeviceController& operator=(const DeviceController&) = delete;
    
    int get() const { return fd_.get(); }
    
    // 资源获取
    drmModeResPtr getResources() const;
    drmModePlaneResPtr getPlaneResources() const;
    
    // 格式支持检查
    bool isFormatSupported(uint32_t format, uint32_t plane_id = 0) const;
    
    // 平面属性查询
    std::vector<uint32_t> getPlaneFormats(uint32_t plane_id) const;
    std::vector<uint64_t> getPlaneModifiers(uint32_t plane_id, uint32_t format) const;
    
private:
    explicit DeviceController(int fd);
    
    FdWrapper fd_;
    mutable drmModeResPtr resources_ = nullptr;
    mutable drmModePlaneResPtr plane_resources_ = nullptr;
};

#endif // DEVICE_CONTROLLER_H