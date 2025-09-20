#include "dma/dmaBuffer.h"

#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <sys/ioctl.h>
#include <system_error>

#include "logger.h"

using namespace DrmDev;

/* 问题: 内核会强制修改我调整过的大小为它认为更好的
 * 但是内核二次调整的结果并不总是符合预期
 * 例如 v4l2 对齐后的要求 size 是 3133440, 通过内存对齐的方法得出的是 3179520
 * 但是内核会修改为 3096576, 导致v4l2入队失败
 * 解决: 保留 pitch 内存对齐方法, 直接将修改后的 pitch 传递给 width, 强制修改所需空间
 * 这样哪怕内核二次对齐, 也不会小于所需空间
 */
std::shared_ptr<DmaBuffer> DmaBuffer::create(uint32_t width, uint32_t height,
                                             uint32_t format, uint32_t required_size, uint32_t offset)
{
    std::lock_guard<std::mutex> lock(fd_mutex);
    if (-1 == fd_ptr->get()) {
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

        if (drmIoctl(fd_ptr->get(), DRM_IOCTL_MODE_CREATE_DUMB, &create_arg) < 0) {
            Logger::log(stderr, "DRM_IOCTL_MODE_CREATE_DUMB failed with align %u: %d\n",
                        align, errno);
            continue;
        }

        if (create_arg.size < required_size) {
            // 销毁 handle, 尝试更大对齐
            drm_mode_destroy_dumb destroy_arg = {};
            destroy_arg.handle = create_arg.handle;
            drmIoctl(fd_ptr->get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
            continue;
        }

        int primefd = exportFD(create_arg);
        if (primefd < 0) {
            Logger::log(stderr, "[DmaBuffer] failed to export prime fd: %d\n", primefd);
            return nullptr;
        }
        dmaBufferData data = {create_arg.handle, width, height,
            format, create_arg.pitch, create_arg.size, offset};
        return std::shared_ptr<DmaBuffer>(
            new DmaBuffer(primefd, data)
        );
    }

    Logger::log(stderr, "Failed to create dumb buffer with required size %u\n", required_size);
    return nullptr;
}

// 无需内存对齐直接分配
std::shared_ptr<DmaBuffer> DmaBuffer::create(uint32_t width, uint32_t height, uint32_t format, uint32_t offset)
{
    std::lock_guard<std::mutex> lock(fd_mutex);
    if (-1 == fd_ptr->get()) {
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
    if (0 > drmIoctl(fd_ptr->get(), DRM_IOCTL_MODE_CREATE_DUMB, &create_arg)) {
        Logger::log(stderr, "DRM_IOCTL_MODE_CREATE_DUMB failed: %d\n", errno);
    }

    int primefd = exportFD(create_arg);
    if (0 > primefd) {
        Logger::log(stderr, "[DmaBuffer] failed to export prime fd: %d\n", primefd);
        return nullptr;
    }
    dmaBufferData data = {create_arg.handle, width, height,
        format, create_arg.pitch, create_arg.size, offset};
    return std::shared_ptr<DmaBuffer>(
        new DmaBuffer(primefd, data)
    );
}
// 从外部导入
std::shared_ptr<DmaBuffer> DmaBuffer::importFromFD(
    int importFd, uint32_t width, uint32_t height, uint32_t format, uint32_t offset)
{
    if (importFd < 0) {
        Logger::log(stderr, "Invalid import fd: %d\n", importFd);
        return nullptr;
    }
    if (width == 0 || height == 0) {
        Logger::log(stderr, "Invalid dimensions: %ux%u\n", width, height);
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(fd_mutex);
    if (-1 == fd_ptr->get()) {
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
    if (drmPrimeFDToHandle(fd_ptr->get(), importFd, &handle) < 0) {
        Logger::log(stderr, "Failed to import DMA-BUF fd: %d\n", importFd);
        return nullptr;
    }
    dmaBufferData data = {handle, width, height, format, pitch, size, offset};
    return std::shared_ptr<DmaBuffer>(
        new DmaBuffer(importFd, data)
    );
}


int DmaBuffer::exportFD(drm_mode_create_dumb &create_arg)
{
    // 将handle导出为fd(drmPrimeHandle -> drmPrimeFD)
    int prime_fd = -1;
    if (0 > drmPrimeHandleToFD(fd_ptr->get(), create_arg.handle, DRM_CLOEXEC | DRM_RDWR, &prime_fd)) {
        drm_mode_destroy_dumb destroy_arg = {};
        destroy_arg.handle = create_arg.handle;
        drmIoctl(fd_ptr->get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
        Logger::log(stderr, "drmPrimeHandleToFD failed");
    }

    /* 如果完全依赖 prime fd，可立即销毁 handle。
    * 如果后续需要 plane attach，必须保留 handle + drm_fd 直到 attach 完成。
    * 立即销毁 handle 实现见 DmaBuffer::cleanup()
    */
    return prime_fd;
}

// 赋值储存数据
DmaBuffer::DmaBuffer(int primeFd, dmaBufferData& data)
    : m_fd(primeFd)
    , data_(data){}

DmaBuffer::~DmaBuffer()
{
    unmap();
    // 回收资源
    cleanup();
}

void DmaBuffer::cleanup() noexcept
{
    fprintf(stdout,"DmaBuffer:closed: fd=%d, handle=%d\n", m_fd, data_.handle);
    if (-1 != m_fd) {
        // 关闭 dambuf fd
        ::close(m_fd);
        m_fd = -1;
    }
    // 销毁 Handle
    std::lock_guard<std::mutex> lock(fd_mutex);
    if (0 != data_.handle && -1 != fd_ptr->get()) {
        drm_mode_destroy_dumb destroy_arg = {};
        destroy_arg.handle = data_.handle;
        drmIoctl(fd_ptr->get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
        data_.handle = 0;
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
        data_    = other.data_;

        other.m_fd     = -1;
        other.data_    = {};
    }
    return *this;
}
