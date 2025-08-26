/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-25 19:18:18
 * @FilePath: /EdgeVision/src/utils/drm/deviceController.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "drm/deviceController.h"

namespace DrmDev {
    std::mutex fd_mutex;
    DrmDevicePtr fd_ptr = nullptr;
}

std::shared_ptr<DeviceController> DeviceController::create(const std::string &path)
{
    if (nullptr == DrmDev::fd_ptr) {
        try{
            int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
            if (-1 == fd) {
                throw std::system_error(errno, std::system_category(), "Failed to open DRM device");
            }

            return std::shared_ptr<DeviceController>(new DeviceController(fd));
        } catch (const std::system_error& ex){
            fprintf(stderr, "DmaBuffer::initialize_drm_fd: %s\n",ex.what());
        }
    }
    return DrmDev::fd_ptr;
}

DeviceController::DeviceController(int fd) : fd_(fd){}

drmModeResPtr DeviceController::getResources() const
{
    return drmModeResPtr();
}

drmModePlaneResPtr DeviceController::getPlaneResources() const
{
    return drmModePlaneResPtr();
}

bool DeviceController::isFormatSupported(uint32_t format, uint32_t plane_id) const
{
    return false;
}

std::vector<uint32_t> DeviceController::getPlaneFormats(uint32_t plane_id) const
{
    return std::vector<uint32_t>();
}

std::vector<uint64_t> DeviceController::getPlaneModifiers(uint32_t plane_id, uint32_t format) const
{
    return std::vector<uint64_t>();
}
