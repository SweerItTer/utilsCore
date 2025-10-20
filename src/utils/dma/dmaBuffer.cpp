#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <sys/ioctl.h>
#include <system_error>

#include "dma/dmaBuffer.h"
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
                                             uint32_t format, uint32_t required_size,
                                             uint32_t offset) {
    std::lock_guard<std::mutex> lock(fd_mutex);
    if (-1 == fd_ptr->get()) {
        fprintf(stderr, "DRM fd not initialized, please call initialize_drm_fd() first\n");
        return nullptr;
    }

    uint32_t bpp = calculate_bpp(format);
    if ((uint32_t)-1 == bpp) {
        fprintf(stderr, "[DmaBuffer] Unsupported format: 0x%x\n", format);
        return nullptr;
    }

    // 按不同对齐方式尝试分配
    constexpr uint32_t align_options[] = {16, 32, 64, 128};
    for (uint32_t align : align_options) {
        // 按字节对齐计算新的 pitch 和 width
        uint32_t pitch_bytes   = ((width * bpp / 8 + align - 1) / align) * align;
        uint32_t aligned_width = pitch_bytes * 8 / bpp;

        drm_mode_create_dumb create_arg = {};
        create_arg.width  = aligned_width; // 直接用对齐后的宽度,避免内核二次对齐
        create_arg.height = height;
        create_arg.bpp    = bpp;

        if (drmIoctl(fd_ptr->get(), DRM_IOCTL_MODE_CREATE_DUMB, &create_arg) < 0) {
            fprintf(stderr, "DRM_IOCTL_MODE_CREATE_DUMB failed with align %u: %d\n",
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
            fprintf(stderr, "[DmaBuffer] failed to export prime fd: %d\n", primefd);
            return nullptr;
        }

        dmaBufferData data = {
            create_arg.handle, width, height,
            format, create_arg.pitch, create_arg.size, offset
        };
        return std::shared_ptr<DmaBuffer>(new DmaBuffer(primefd, data));
    }

    fprintf(stderr, "Failed to create dumb buffer with required size %u\n", required_size);
    return nullptr;
}


// 无需对齐的 dumb buffer 创建
std::shared_ptr<DmaBuffer> DmaBuffer::create(uint32_t width, uint32_t height,
                                             uint32_t format, uint32_t offset) {
    std::lock_guard<std::mutex> lock(fd_mutex);
    if (-1 == fd_ptr->get()) {
        fprintf(stderr, "DRM fd not initialized, please call initialize_drm_fd() first");
        return nullptr;
    }

    uint32_t bpp = calculate_bpp(format);
    if ((uint32_t)-1 == bpp) {
        fprintf(stderr, "[DmaBuffer] Unsupported format: 0x%x\n", format);
        return nullptr;
    }

    drm_mode_create_dumb create_arg = {};
    create_arg.width  = width;
    create_arg.height = height;
    create_arg.bpp    = bpp;

    if (0 > drmIoctl(fd_ptr->get(), DRM_IOCTL_MODE_CREATE_DUMB, &create_arg)) {
        fprintf(stderr, "DRM_IOCTL_MODE_CREATE_DUMB failed: %d\n", errno);
        return nullptr;
    }

    int primefd = exportFD(create_arg);
    if (0 > primefd) {
        fprintf(stderr, "[DmaBuffer] failed to export prime fd: %d\n", primefd);
        return nullptr;
    }

    dmaBufferData data = {
        create_arg.handle, width, height,
        format, create_arg.pitch, create_arg.size, offset
    };
    return std::shared_ptr<DmaBuffer>(new DmaBuffer(primefd, data));
}

// 从外部导入
std::shared_ptr<DmaBuffer> DmaBuffer::importFromFD(
    int importFd, uint32_t width, uint32_t height, uint32_t format, uint32_t size)
{
    if (importFd < 0) {
        fprintf(stderr, "Invalid import fd: %d\n", importFd);
        return nullptr;
    }
    if (0 == width || 0 == height) {
        fprintf(stderr, "Invalid dimensions: %ux%u\n", width, height);
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(fd_mutex);
    if (-1 == fd_ptr->get()) {
        fprintf(stderr, "DRM fd not initialized, please call initialize_drm_fd() first\n");
        return nullptr;
    }

    // Step 1: 把 fd 转成 DRM handle
    uint32_t handle = 0;
    if (drmPrimeFDToHandle(fd_ptr->get(), importFd, &handle) < 0) {
        fprintf(stderr, "Failed to import DMA-BUF fd: %d\n", importFd);
        return nullptr;
    }

    // 查询实际 pitch
    uint32_t pitch = size / height;

    drm_mode_map_dumb map_arg = {};
    map_arg.handle = handle;
    if (drmIoctl(fd_ptr->get(), DRM_IOCTL_MODE_MAP_DUMB, &map_arg) < 0) {
        fprintf(stderr, "DRM_IOCTL_MODE_MAP_DUMB failed for handle %u\n", handle);
        return nullptr;
    }

    dmaBufferData data = {handle, width, height, format, pitch, size, static_cast<uint32_t>(map_arg.offset)};
    return std::shared_ptr<DmaBuffer>(new DmaBuffer(importFd, data, true));
}


/* 将handle导出为fd(drmPrimeHandle -> drmPrimeFD) */
int DmaBuffer::exportFD(drm_mode_create_dumb &create_arg) {
    int prime_fd = -1;
    if (0 > drmPrimeHandleToFD(fd_ptr->get(), create_arg.handle, DRM_CLOEXEC | DRM_RDWR, &prime_fd)) {
        drm_mode_destroy_dumb destroy_arg = {};
        destroy_arg.handle = create_arg.handle;
        drmIoctl(fd_ptr->get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
        fprintf(stderr, "drmPrimeHandleToFD failed");
    }

    /* 如果完全依赖 prime fd，可立即销毁 handle。
    * 如果后续需要 plane attach，必须保留 handle + drm_fd 直到 attach 完成。
    * 立即销毁 handle 实现见 DmaBuffer::cleanup()
    */
    return prime_fd;
}


// ---------------------- 构造/析构/cleanup ----------------------

DmaBuffer::DmaBuffer(int primeFd, dmaBufferData& data)
    : m_fd(primeFd)
    , data_(data) {}

DmaBuffer::DmaBuffer(int primeFd, dmaBufferData& data, bool isimport)
    : m_fd(primeFd)
    , data_(data), isimport_(isimport){}

DmaBuffer::~DmaBuffer() {
    unmap();
    // 回收资源
    cleanup();
}

void DmaBuffer::cleanup() noexcept {
    if (isimport_) return;
    // fprintf(stdout, "DmaBuffer: closed: fd=%d, handle=%d\n", m_fd, data_.handle);

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


// ---------------------- 移动语义 ----------------------

DmaBuffer::DmaBuffer(DmaBuffer&& other) noexcept {
    *this = std::move(other);
}

DmaBuffer& DmaBuffer::operator=(DmaBuffer&& other) noexcept {
    if (this != &other) {
        cleanup();
        m_fd  = other.m_fd;
        data_ = other.data_;
        other.m_fd  = -1;
        other.data_ = {};
    }
    return *this;
}


// ---------- 基本信息 ----------
int DmaBuffer::fd() const noexcept { return m_fd; }
uint32_t DmaBuffer::handle() const noexcept { return data_.handle; }
uint32_t DmaBuffer::width()  const noexcept { return data_.width;  }
uint32_t DmaBuffer::height() const noexcept { return data_.height; }
uint32_t DmaBuffer::format() const noexcept { return data_.format; }
uint32_t DmaBuffer::pitch()  const noexcept { return data_.pitch;  }
uint32_t DmaBuffer::size()   const noexcept { return data_.size;   }
uint32_t DmaBuffer::offset() const noexcept { return data_.offset; }

// ---------- map/unmap ----------
uint8_t* DmaBuffer::map() {
    if (m_fd < 0) {
        throw std::runtime_error("Invalid DMABUF fd");
    }
    if (nullptr != mappedPtr_) {
        return mappedPtr_;
    }
    void* ptr = mmap(nullptr, data_.size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
    if (MAP_FAILED == ptr) {
        perror("mmap failed");
        return nullptr;
    }
    mappedPtr_ = reinterpret_cast<uint8_t*>(ptr);
    return mappedPtr_;
}

void DmaBuffer::unmap() {
    if (nullptr != mappedPtr_) {
        munmap(mappedPtr_, data_.size);
        mappedPtr_ = nullptr;
    }
}

// ---------- MappedView ----------
DmaBuffer::MappedView::MappedView(DmaBuffer& owner, uint8_t* ptr)
    : owner_(owner), ptr_(ptr) {}

DmaBuffer::MappedView::~MappedView() {
    if (nullptr != ptr_) {
        owner_.unmap();
    }
}

DmaBuffer::MappedView::MappedView(MappedView&& other) noexcept
    : owner_(other.owner_), ptr_(other.ptr_) {
    other.ptr_ = nullptr;
}

DmaBuffer::MappedView& DmaBuffer::MappedView::operator=(MappedView&& other) noexcept {
    if (this != &other) {
        if (nullptr != ptr_) {
            owner_.unmap();
        }
        ptr_ = other.ptr_;
        other.ptr_ = nullptr;
        // owner_ 保持引用不变，不赋值
    }
    return *this;
}


uint8_t* DmaBuffer::MappedView::get() { return ptr_; }
DmaBuffer::MappedView::operator uint8_t*() { return ptr_; }

// ---------- scopedMap ----------
DmaBuffer::MappedView DmaBuffer::scopedMap() {
    return MappedView(*this, map());
}
