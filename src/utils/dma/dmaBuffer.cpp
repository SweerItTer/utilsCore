#include "dma/dmaBuffer.h"

#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <sys/ioctl.h>
#include <system_error>

#include "logger.h"

FdWrapper DmaBuffer::drm_fd;

// 根据指定大小分配(在需求内存对齐的情况下)
std::shared_ptr<DmaBuffer> DmaBuffer::create(uint32_t width, uint32_t height, uint32_t format, uint32_t required_size)
{
    if (-1 == drm_fd.get()) {
        Logger::log(stderr, "DRM fd not initialized, please call initialize_drm_fd() first\n");
        return nullptr;
    }

    uint32_t bpp = calculate_bpp(format);
    if ((uint32_t)-1 == bpp) {
        Logger::log(stderr, "[DmaBuffer] Unsupported format: 0x%x\n", format);
        return nullptr;
    }

    drm_mode_create_dumb create_arg = {};
    create_arg.width  = width;
    create_arg.height = height;
    create_arg.bpp    = bpp;

    // DRM dumb buffer 对齐要求（常见 64 字节对齐）
    constexpr uint32_t align_options[] = {16, 32, 64, 128};
    for (uint32_t align : align_options) {
        create_arg.pitch = ((width * bpp / 8 + align - 1) / align) * align;  // 向上对齐
        create_arg.size  = create_arg.pitch * height;

        if (create_arg.size < required_size) {
            continue; // 尝试更大对齐
        }

        if (0 > drmIoctl(drm_fd.get(), DRM_IOCTL_MODE_CREATE_DUMB, &create_arg)) {
            Logger::log(stderr, "DRM_IOCTL_MODE_CREATE_DUMB failed with align %u: %d\n", align, errno);
            continue; // 尝试下一个对齐
        }

        int primefd = exportFD(create_arg);
        if (primefd < 0) {
            Logger::log(stderr, "[DmaBuffer] failed to export prime fd: %d\n", primefd);
            return nullptr;
        }

        return std::shared_ptr<DmaBuffer>(
            new DmaBuffer(primefd, create_arg.handle, width, height,
                          format, create_arg.pitch, create_arg.size));
    }

    Logger::log(stderr, "Failed to create dumb buffer with required size %u\n", required_size);
    return nullptr;
}

// 无需内存对齐直接分配
std::shared_ptr<DmaBuffer> DmaBuffer::create(uint32_t width, uint32_t height, uint32_t format)
{
    if (-1 == drm_fd.get()) {
        Logger::log(stderr, "DRM fd not initialized, please call initialize_drm_fd() first");
        return nullptr;
    }

    drm_mode_create_dumb create_arg = {};
    create_arg.width  = width;
    create_arg.height = height;

    uint32_t bpp = calculate_bpp(format);
    if (-1 == bpp) {
        Logger::log(stderr, "[DmaBuffer] Unsupported format: 0x%x\n", format);
        return nullptr;
    }

    create_arg.bpp = bpp;
    if (0 > drmIoctl(drm_fd.get(), DRM_IOCTL_MODE_CREATE_DUMB, &create_arg)) {
        Logger::log(stderr, "DRM_IOCTL_MODE_CREATE_DUMB failed: %d\n", errno);
    }

    int primefd = exportFD(create_arg);
    if (0 > primefd) {
        Logger::log(stderr, "[DmaBuffer] failed to export prime fd: %d\n", primefd);
        return nullptr;
    }

    return std::shared_ptr<DmaBuffer>(
        new DmaBuffer(primefd, create_arg.handle, width, height,
                      format, create_arg.pitch, create_arg.size)
    );
}

int DmaBuffer::exportFD(drm_mode_create_dumb& create_arg)
{
    // 将handle导出为fd(drmPrimeHandle -> drmPrimeFD)
    int prime_fd = -1;
    if (0 > drmPrimeHandleToFD(drm_fd.get(), create_arg.handle, DRM_CLOEXEC | DRM_RDWR, &prime_fd)) {
        drm_mode_destroy_dumb destroy_arg = {};
        destroy_arg.handle = create_arg.handle;
        drmIoctl(drm_fd.get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
        Logger::log(stderr, "drmPrimeHandleToFD failed");
    }

    /* 如果完全依赖 prime fd，可立即销毁 handle。
    * 如果后续需要 plane attach，必须保留 handle + drm_fd 直到 attach 完成。
    * 此处注释为立即销毁 handle。
    */
    // drm_mode_destroy_dumb destroy_arg = {};
    // destroy_arg.handle = create_arg.handle;
    // drmIoctl(drm_fd.get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);;
    return prime_fd;
}

// 赋值储存数据
DmaBuffer::DmaBuffer(int prime_fd, uint32_t handle, uint32_t width,
                     uint32_t height, uint32_t format,
                     uint32_t pitch, uint32_t size)
    : m_fd(prime_fd)
    , m_handle(handle)
    , m_width(width)
    , m_height(height)
    , m_format(format)
    , m_pitch(pitch)
    , m_size(size){}

DmaBuffer::~DmaBuffer()
{
    // 回收资源
    cleanup();
}

void DmaBuffer::cleanup() noexcept
{
    Logger::log(stdout,"DmaBuffer::cleanup(): fd=%d, handle=%d\n", m_fd, m_handle);
    if (-1 != m_fd) {
        // 关闭 dambuf fd
        ::close(m_fd);
        m_fd = -1;
    }
    // 销毁 Handle
    if (0 != m_handle && -1 != drm_fd.get()) {
        drm_mode_destroy_dumb destroy_arg = {};
        destroy_arg.handle = m_handle;
        drmIoctl(drm_fd.get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
        m_handle = 0;
    }
}

// 打开全局 drm_fd (后续仍会使用到(不仅仅是导出dmabuf fd))
void DmaBuffer::initialize_drm_fd()
{
    if (-1 == drm_fd.get()) {
        try{
            int fd = ::open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
            if (-1 == fd) {
                throw std::system_error(errno, std::system_category(), "Failed to open DRM device");
            }
            drm_fd = FdWrapper(fd);
        } catch (const std::system_error& ex){
            Logger::log(stderr, "DmaBuffer::initialize_drm_fd: %s\n",ex.what());
        }
    }
}

void DmaBuffer::close_drm_fd()
{
    drm_fd = FdWrapper(); // 析构自动关闭
}

// 移动构造
DmaBuffer::DmaBuffer(DmaBuffer &&other) noexcept
{
    *this = std::move(other);
}

// 移动操作符
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
