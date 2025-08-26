#include "dma/dmaBuffer.h"

#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <sys/ioctl.h>
#include <system_error>

#include "logger.h"

/* 问题: 内核会强制修改我调整过的大小为它认为更好的
 * 但是内核二次调整的结果并不总是符合预期
 * 例如 v4l2 对齐后的要求 size 是 3133440, 通过内存对齐的方法得出的是 3179520
 * 但是内核会修改为 3096576, 导致v4l2入队失败
 * 解决: 保留 pitch 内存对齐方法, 直接将修改后的 pitch 传递给 width, 强制修改所需空间
 * 这样哪怕内核二次对齐, 也不会小于所需空间
 */
std::shared_ptr<DmaBuffer> DmaBuffer::create(uint32_t width, uint32_t height,
                                             uint32_t format, uint32_t required_size)
{
    std::lock_guard<std::mutex> lock(DrmDev::fd_mutex);
    if (-1 == DrmDev::fd_ptr->get()) {
        Logger::log(stderr, "DRM fd not initialized, please call initialize_drm_fd() first\n");
        return nullptr;
    }

    uint32_t bpp = calculate_bpp(format);
    if ((uint32_t)-1 == bpp) {
        Logger::log(stderr, "[DmaBuffer] Unsupported format: 0x%x\n", format);
        return nullptr;
    }

    constexpr uint32_t align_options[] = {16, 32, 64, 128};
    for (uint32_t align : align_options) {
        // 按字节对齐计算新的 pitch 和 width
        uint32_t pitch_bytes = ((width * bpp / 8 + align - 1) / align) * align;
        uint32_t aligned_width = pitch_bytes * 8 / bpp;

        drm_mode_create_dumb create_arg = {};
        create_arg.width  = aligned_width; // 直接将对齐后的宽度传入(避免内核二次对齐)
        create_arg.height = height;
        create_arg.bpp    = bpp;

        if (drmIoctl(DrmDev::fd_ptr->get(), DRM_IOCTL_MODE_CREATE_DUMB, &create_arg) < 0) {
            Logger::log(stderr, "DRM_IOCTL_MODE_CREATE_DUMB failed with align %u: %d\n",
                        align, errno);
            continue;
        }

        if (create_arg.size < required_size) {
            // 销毁 handle, 尝试更大对齐
            drm_mode_destroy_dumb destroy_arg = {};
            destroy_arg.handle = create_arg.handle;
            drmIoctl(DrmDev::fd_ptr->get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
            continue;
        }
        // std::cout << "alignedWidth:\t"<< aligned_width << "\theight\t" << height
        //     << "\tbpp:\t" << bpp << "\tpitch:\t" << create_arg.pitch
        //     << "\tcreatedSize:\t" << create_arg.size << "\trequiredSize: \t" << required_size << "\n";

        int primefd = exportFD(create_arg);
        if (primefd < 0) {
            Logger::log(stderr, "[DmaBuffer] failed to export prime fd: %d\n", primefd);
            return nullptr;
        }

        return std::shared_ptr<DmaBuffer>(
            new DmaBuffer(primefd, create_arg.handle, aligned_width, height,
                          format, create_arg.pitch, create_arg.size));
    }

    Logger::log(stderr, "Failed to create dumb buffer with required size %u\n", required_size);
    return nullptr;
}

// 无需内存对齐直接分配
std::shared_ptr<DmaBuffer> DmaBuffer::create(uint32_t width, uint32_t height, uint32_t format)
{
    std::lock_guard<std::mutex> lock(DrmDev::fd_mutex);
    if (-1 == DrmDev::fd_ptr->get()) {
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
    if (0 > drmIoctl(DrmDev::fd_ptr->get(), DRM_IOCTL_MODE_CREATE_DUMB, &create_arg)) {
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
// 从外部导入
std::shared_ptr<DmaBuffer> DmaBuffer::importFromFD(
    int importFd, uint32_t width, uint32_t height, uint32_t format)
{
    if (importFd < 0) {
        Logger::log(stderr, "Invalid import fd: %d\n", importFd);
        return nullptr;
    }
    if (width == 0 || height == 0) {
        Logger::log(stderr, "Invalid dimensions: %ux%u\n", width, height);
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(DrmDev::fd_mutex);
    if (-1 == DrmDev::fd_ptr->get()) {
        Logger::log(stderr, "DRM fd not initialized, please call initialize_drm_fd() first\n");
        return nullptr;
    }

    uint32_t bpp = calculate_bpp(format);
    if ((uint32_t)-1 == bpp) {
        Logger::log(stderr, "[DmaBuffer] Unsupported format: 0x%x\n", format);
        return nullptr;
    }

    uint32_t raw_pitch = (width * bpp + 7) / 8;
    uint32_t align = 64;
    uint32_t pitch = (raw_pitch + align - 1) / align * align;
    uint32_t size = pitch * height;

    uint32_t handle = 0;
    if (drmPrimeFDToHandle(DrmDev::fd_ptr->get(), importFd, &handle) < 0) {
        Logger::log(stderr, "Failed to import DMA-BUF fd: %d\n", importFd);
        return nullptr;
    }

    return std::shared_ptr<DmaBuffer>(
        new DmaBuffer(importFd, handle, width, height, format, pitch, size));
}


int DmaBuffer::exportFD(drm_mode_create_dumb &create_arg)
{
    // 将handle导出为fd(drmPrimeHandle -> drmPrimeFD)
    int prime_fd = -1;
    if (0 > drmPrimeHandleToFD(DrmDev::fd_ptr->get(), create_arg.handle, DRM_CLOEXEC | DRM_RDWR, &prime_fd)) {
        drm_mode_destroy_dumb destroy_arg = {};
        destroy_arg.handle = create_arg.handle;
        drmIoctl(DrmDev::fd_ptr->get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
        Logger::log(stderr, "drmPrimeHandleToFD failed");
    }

    /* 如果完全依赖 prime fd，可立即销毁 handle。
    * 如果后续需要 plane attach，必须保留 handle + drm_fd 直到 attach 完成。
    * 立即销毁 handle 实现见 DmaBuffer::cleanup()
    */
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
    // fprintf(stdout,"DmaBuffer:closed: fd=%d, handle=%d\n", m_fd, m_handle);
    if (-1 != m_fd) {
        // 关闭 dambuf fd
        ::close(m_fd);
        m_fd = -1;
    }
    // 销毁 Handle
    std::lock_guard<std::mutex> lock(DrmDev::fd_mutex);
    if (0 != m_handle && -1 != DrmDev::fd_ptr->get()) {
        drm_mode_destroy_dumb destroy_arg = {};
        destroy_arg.handle = m_handle;

        drmIoctl(DrmDev::fd_ptr->get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
        m_handle = 0;
    }
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
