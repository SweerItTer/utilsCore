#include "dma/dmaBuffer.h"

#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <sys/ioctl.h>
#include <system_error>

FdWrapper DmaBuffer::drm_fd;

std::shared_ptr<DmaBuffer> DmaBuffer::create(uint32_t width, uint32_t height, uint32_t format)
{
    if (-1 == drm_fd.get()) {
        throw std::runtime_error("DRM fd not initialized, please call initialize_drm_fd() first");
    }

    drm_mode_create_dumb create_arg = {};
    create_arg.width  = width;
    create_arg.height = height;

    uint32_t bpp = calculate_bpp(format);
    if (-1 == bpp) {
        throw std::runtime_error("Unsupported pixel format");
    }
    create_arg.bpp = bpp;

    if (0 > drmIoctl(drm_fd.get(), DRM_IOCTL_MODE_CREATE_DUMB, &create_arg)) {
        throw std::runtime_error("DRM_IOCTL_MODE_CREATE_DUMB failed");
    }

    int prime_fd = -1;
    if (0 > drmPrimeHandleToFD(drm_fd.get(), create_arg.handle, DRM_CLOEXEC | DRM_RDWR, &prime_fd)) {
        drm_mode_destroy_dumb destroy_arg = {};
        destroy_arg.handle = create_arg.handle;
        drmIoctl(drm_fd.get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
        throw std::runtime_error("drmPrimeHandleToFD failed");
    }

    /* 如果完全依赖 prime fd，可立即销毁 handle。
     * 如果后续需要 plane attach，必须保留 handle + drm_fd 直到 attach 完成。
     * 此处演示立即销毁 handle。
     */
    // drm_mode_destroy_dumb destroy_arg = {};
    // destroy_arg.handle = create_arg.handle;
    // drmIoctl(drm_fd.get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);;

    return std::shared_ptr<DmaBuffer>(
        new DmaBuffer(prime_fd, create_arg.handle, width, height,
                      format, create_arg.pitch, create_arg.size)
    );
}

DmaBuffer::DmaBuffer(int prime_fd, uint32_t handle, uint32_t width,
                     uint32_t height, uint32_t format,
                     uint32_t pitch, uint32_t size)
    : m_fd(prime_fd)
    , m_handle(handle)
    , m_width(width)
    , m_height(height)
    , m_format(format)
    , m_pitch(pitch)
    , m_size(size)
{
}

DmaBuffer::~DmaBuffer()
{
    cleanup();
}

void DmaBuffer::cleanup() noexcept
{
    if (-1 != m_fd) {
        ::close(m_fd);
        m_fd = -1;
    }

    if (0 != m_handle && -1 != drm_fd.get()) {
        drm_mode_destroy_dumb destroy_arg = {};
        destroy_arg.handle = m_handle;
        drmIoctl(drm_fd.get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
        m_handle = 0;
    }
    fprintf(stdout,"DmaBuffer::cleanup\n");
}

void DmaBuffer::initialize_drm_fd()
{
    if (-1 == drm_fd.get()) {
        int fd = ::open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
        if (-1 == fd) {
            throw std::system_error(errno, std::system_category(), "Failed to open DRM device");
        }
        drm_fd = FdWrapper(fd);
    }
}

void DmaBuffer::close_drm_fd()
{
    drm_fd = FdWrapper(); // 析构自动关闭
}

DmaBuffer::DmaBuffer(DmaBuffer &&other) noexcept
{
    *this = std::move(other);
}

DmaBuffer& DmaBuffer::operator=(DmaBuffer&& other) noexcept
{
    if (this != &other) {
        cleanup();

        m_fd     = other.m_fd;
        m_handle = other.m_handle;
        m_width  = other.m_width;
        m_height = other.m_height;
        m_format = other.m_format;
        m_pitch  = other.m_pitch;
        m_size   = other.m_size;

        other.m_fd     = -1;
        other.m_handle =  0;
        other.m_width  =  0;
        other.m_height =  0;
        other.m_format =  0;
        other.m_pitch  =  0;
        other.m_size   =  0;
    }
    return *this;
}
