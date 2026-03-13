#include <cstdint>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>
#include <sys/ioctl.h>
#include <system_error>
#include <utility>

#include "dma/dmaBuffer.h"
#include "drm/deviceController.h"
#include "logger.h"

using namespace DrmDev;

namespace {

uint64_t hashCombine(uint64_t seed, uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
}

bool fillFileIdentity(int dmaBufferFd, dev_t& fileSystemDevice, ino_t& fileSystemInode) {
    struct stat statBuffer {};
    if (dmaBufferFd < 0 || ::fstat(dmaBufferFd, &statBuffer) < 0) {
        return false;
    }
    fileSystemDevice = statBuffer.st_dev;
    fileSystemInode = statBuffer.st_ino;
    return true;
}

} // namespace

// ---------------------- 工厂函数 ----------------------

/**
 * @brief 将 DRM handle 导出为 DMA-BUF fd
 *
 * 如果完全依赖 prime fd, 可立即销毁 handle
 * 如果后续需要 plane attach, 必须保留 handle + drm_fd 直到 attach 完成
 *
 * @param handle DRM handle
 * @return int 成功返回 fd, 失败返回 -1
 */
static int exportFD(uint32_t handle) {
    if (handle == 0) {
        fprintf(stderr, "[DmaBuffer] Invalid handle 0\n");
        return -1;
    }
    int prime_fd = -1;

    {
        std::lock_guard<std::mutex> lock(fd_mutex);
        if (0 > drmPrimeHandleToFD(fd_ptr->get(), handle, DRM_CLOEXEC | DRM_RDWR, &prime_fd)) {
            drm_mode_destroy_dumb destroy_arg = {};
            destroy_arg.handle = handle;
            drmIoctl(fd_ptr->get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
            fprintf(stderr, "[DmaBuffer] drmPrimeHandleToFD failed");
        }
    }

    /* 如果完全依赖 prime fd, 可立即销毁 handle。
     * 如果后续需要 plane attach, 必须保留 handle + drm_fd 直到 attach 完成。
     * 立即销毁 handle 实现见 DmaBuffer::cleanup()
     */
    return prime_fd;
}

/**
 * @brief 获取指定 plane 的宽高比例及 BPP
 *
 * @param format 像素格式
 * @param planeIndex plane 索引
 * @param ratioW 输出: 宽度比例
 * @param ratioH 输出: 高度比例
 * @param bpp 输出: 每像素位数
 * @return true 支持该格式
 * @return false 不支持该格式
 */
static bool getPlaneBpp(uint32_t format, uint32_t planeIndex,
                        float &ratioW, float &ratioH, uint32_t &bpp)
{
    PlaneFormatInfo info = getPlaneInfo(format);
    ratioW = info(planeIndex).w;
    ratioH = info(planeIndex).h;

    bpp = calculate_bpp(format);
    if (bpp == 0) {
        fprintf(stderr, "[DmaBuffer] Unsupported format: 0x%x\n", format);
        return false;
    }
    return true;
}

/**
 * @brief 按指定对齐方式计算对齐后的宽高
 *
 * @param width 原始宽度
 * @param height 原始高度
 * @param ratioW 宽度比例
 * @param ratioH 高度比例
 * @param align 对齐字节数
 * @return std::pair<uint32_t,uint32_t> 对齐后的宽度和高度
 */
static std::pair<uint32_t, uint32_t> calculateAlignedSize(uint32_t width, uint32_t height,
                                 float ratioW, float ratioH, uint32_t align)
{
    uint32_t alignedWidth  = static_cast<uint32_t>((width * ratioW + align - 1) / align) * align;
    uint32_t alignedHeight = static_cast<uint32_t>((height * ratioH + align - 1) / align) * align;
    return std::pair<uint32_t, uint32_t>(alignedWidth, alignedHeight);
}

/**
 * @brief 尝试创建 DRM dumb buffer
 *
 * 会根据不同对齐方式循环尝试, 如果大小不够会销毁并继续尝试
 *
 * @param data 输入输出: DMA buffer 数据结构, 需要至少填充 width/height/format/offset
 * @param requiredSize 需要的最小字节大小
 * @param planeIndex plane 索引
 * @param ratioW 宽度比例
 * @param ratioH 高度比例
 * @param bpp 每像素位数
 * @return true 创建成功
 * @return false 创建失败
 */
bool DmaBuffer::tryCreateDumbBuffer(DmaBufferData &data, uint32_t requiredSize, uint32_t planeIndex,
                                float ratioW, float ratioH, uint32_t bpp) {
    uint32_t width = data.width;
    uint32_t height = data.height;
    if (width == 0 || height == 0) {
        fprintf(stderr, "[DmaBuffer] Invalid dimensions: %ux%u\n", width, height);
        return false;
    }

    constexpr uint32_t alignOptions[] = {8, 16, 32, 64, 128};
    for (uint32_t align : alignOptions) {
        // 按字节对齐计算新的 width 和 height
        std::pair<uint32_t, uint32_t> alignedSize = calculateAlignedSize(width, height, ratioW, ratioH, align);

        drm_mode_create_dumb createArg{};
        // 直接用对齐后的宽度, 避免内核再对齐导致大小不符合预期
        createArg.width  = alignedSize.first; // aligned width
        createArg.height = alignedSize.second;// aligned height
        createArg.bpp    = bpp;

        {
            std::lock_guard<std::mutex> lock(fd_mutex);
            // 尝试创建 dumb buffer
            if (drmIoctl(fd_ptr->get(), DRM_IOCTL_MODE_CREATE_DUMB, &createArg) < 0) {
                fprintf(stderr, "[DmaBuffer] DRM_IOCTL_MODE_CREATE_DUMB failed with align %u: %d\n",
                        align, errno);
                continue;
            }
        }
           
        // 检查大小是否满足要求
        if (createArg.size < requiredSize) {
            drm_mode_destroy_dumb destroyArg{};
            destroyArg.handle = createArg.handle;
            std::lock_guard<std::mutex> lock(fd_mutex);
            drmIoctl(fd_ptr->get(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroyArg);
            continue;
        }

        data.handle = createArg.handle;
        data.pitch  = createArg.pitch;
        data.size   = createArg.size;

        return true;
    }
    std::cerr << "[DmaBuffer] Failed to create dumb buffer with required size "
              << requiredSize << " after trying all alignments\n";
    return false;
}

// ---------------------- 创建 DMABUF Ptr ----------------------

/* 问题: 内核会强制修改我调整过的大小为它认为更好的
 * 但是内核二次调整的结果并不总是符合预期
 * 例如 v4l2 对齐后的要求 size 是 3133440, 通过内存对齐的方法得出的是 3179520
 * 但是内核会修改为 3096576, 导致v4l2入队失败
 * 解决: 保留 pitch 内存对齐方法, 直接将修改后的 pitch 传递给 width, 强制修改所需空间
 * 这样哪怕内核二次对齐, 也不会小于所需空间
 */
std::shared_ptr<DmaBuffer> DmaBuffer::create(uint32_t width, uint32_t height,
                                             uint32_t format, uint32_t requiredSize,
                                             uint32_t offset, uint32_t planeIndex) {
    {
        std::lock_guard<std::mutex> lock(fd_mutex);
        if (!fd_ptr || -1 == fd_ptr->get()) {
            fprintf(stderr, "[DmaBuffer] DRM fd not initialized, please call initialize_drm_fd() first\n");
            return nullptr;
        }
    }
    if (requiredSize == 0) {
        fprintf(stderr, "[DmaBuffer] Invalid required size 0\n");
        return nullptr;
    }
    float ratioW, ratioH;
    uint32_t bpp;
    if (!getPlaneBpp(format, planeIndex, ratioW, ratioH, bpp)) {
        return nullptr;
    }

    DmaBufferData data {};
    data.width = width;
    data.height = height;
    data.format = format;
    data.offset = offset;
    data.channel = planeIndex;

    if (!tryCreateDumbBuffer(data, requiredSize, planeIndex, ratioW, ratioH, bpp)) {
        fprintf(stderr, "[DmaBuffer] Failed to create (%ux%u) buffer\n\t\t bpp: %u; required size %u;\n", 
                width, height, bpp, requiredSize);
        return nullptr;
    }
    
    // 导出为 dma-buf fd
    int primeFd = exportFD(data.handle);
    if (primeFd < 0) {
        fprintf(stderr, "[DmaBuffer] failed to export prime fd: %d\n", primeFd);
        return nullptr;
    }

    return std::shared_ptr<DmaBuffer>(new DmaBuffer(primeFd, data, false));
}

// 无需对齐的 dumb buffer 创建
std::shared_ptr<DmaBuffer> DmaBuffer::create(uint32_t width, uint32_t height,
                                             uint32_t format, uint32_t offset, uint32_t planeIndex) {
    float ratioW, ratioH;
    uint32_t bpp;
    if (!getPlaneBpp(format, planeIndex, ratioW, ratioH, bpp)) {
        return nullptr;
    }

    // 当前层所需字节数
    uint32_t requiredSize = static_cast<uint32_t>(width * ratioW * height * ratioH * bpp / 8);
    return create(width, height, format, requiredSize, offset, planeIndex);
}

// 从外部导入
std::shared_ptr<DmaBuffer> DmaBuffer::importFromFD(
    int importFd, uint32_t width, uint32_t height, uint32_t format, uint32_t size, uint32_t offset)
{
    if (importFd < 0) {
        fprintf(stderr, "[DmaBuffer] Invalid import fd: %d\n", importFd);
        return nullptr;
    }
    if (0 == width || 0 == height) {
        fprintf(stderr, "[DmaBuffer] Invalid dimensions: %ux%u\n", width, height);
        return nullptr;
    }

    uint32_t handle = 0;
    {
        std::lock_guard<std::mutex> lock(fd_mutex);
        if (!fd_ptr || -1 == fd_ptr->get()) {
            fprintf(stderr, "[DmaBuffer] DRM fd not initialized, please call initialize_drm_fd() first\n");
            return nullptr;
        }

        // 把 fd 转成 DRM handle
        if (drmPrimeFDToHandle(fd_ptr->get(), importFd, &handle) < 0) {
            fprintf(stderr, "[DmaBuffer] Failed to import DMA-BUF fd: %d\n", importFd);
            return nullptr;
        }
    }
    if (0 == handle) {
        fprintf(stderr, "[DmaBuffer] Imported handle is 0 for fd: %d\n", importFd);
        return nullptr;
    }

    // 查询实际 pitch
    uint32_t pitch = size / height;

    DmaBufferData data = {handle, width, height, format, pitch, size, offset, 0};
    return std::shared_ptr<DmaBuffer>(new DmaBuffer(importFd, data, true));
}

// ---------------------- 构造/析构/cleanup ----------------------
DmaBuffer::DmaBuffer(int primeFd, DmaBufferData& data, bool isImported)
    : primeFd_(primeFd)
    , data_(data)
    , isImported_(isImported) {
    buildBufferIdentity();
}

DmaBuffer::~DmaBuffer() {
    unmap();
    // 回收资源
    cleanup();
}

void DmaBuffer::cleanup() noexcept {
    if (isImported_) return;
    // fprintf(stdout, "DmaBuffer: closed: fd=%d, handle=%d\n", primeFd_, data_.handle);

    if (-1 != primeFd_) {
        // 关闭 dambuf fd
        ::close(primeFd_);
        primeFd_ = -1;
    }
    // 销毁 Handle
    if (0 == data_.handle) return;
    std::lock_guard<std::mutex> lock(fd_mutex);
    if (-1 != fd_ptr->get()) {
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
        unmap();
        cleanup();
        primeFd_  = other.primeFd_;
        data_ = other.data_;
        mappedPtr_ = other.mappedPtr_;
        isImported_ = other.isImported_;
        bufferIdentity_ = std::move(other.bufferIdentity_);
        other.primeFd_  = -1;
        other.data_ = {};
        other.mappedPtr_ = nullptr;
        other.isImported_ = false;
        other.bufferIdentity_ = {};
    }
    return *this;
}


// ---------- 基本信息 ----------
const int DmaBuffer::fd() const noexcept { return primeFd_; }
const uint32_t DmaBuffer::handle() const noexcept { return data_.handle; }
const uint32_t DmaBuffer::width()  const noexcept { return data_.width;  }
const uint32_t DmaBuffer::height() const noexcept { return data_.height; }
const uint32_t DmaBuffer::format() const noexcept { return data_.format; }
const uint32_t DmaBuffer::pitch()  const noexcept { return data_.pitch;  }
const uint32_t DmaBuffer::size()   const noexcept {
    if (data_.size > UINT32_MAX) {
        return UINT32_MAX;
    }
    return static_cast<uint32_t>(data_.size);
}
const uint64_t DmaBuffer::size64() const noexcept {
    return data_.size;
}
const uint32_t DmaBuffer::offset() const noexcept { return data_.offset; }
const uint32_t DmaBuffer::channel() const noexcept { return data_.channel; }
const DmaBuffer::BufferIdentity& DmaBuffer::identity() const noexcept { return bufferIdentity_; }
uint64_t DmaBuffer::identityHash() const noexcept { return bufferIdentity_.identityHash; }
bool DmaBuffer::sameIdentity(const DmaBuffer& other) const noexcept {
    const auto& leftIdentity = bufferIdentity_;
    const auto& rightIdentity = other.bufferIdentity_;
    if (leftIdentity.width != rightIdentity.width ||
        leftIdentity.height != rightIdentity.height ||
        leftIdentity.format != rightIdentity.format ||
        leftIdentity.modifier != rightIdentity.modifier ||
        leftIdentity.planeDescriptors.size() != rightIdentity.planeDescriptors.size()) {
        return false;
    }

    for (std::size_t planeIndex = 0; planeIndex < leftIdentity.planeDescriptors.size(); ++planeIndex) {
        const auto& leftPlane = leftIdentity.planeDescriptors[planeIndex];
        const auto& rightPlane = rightIdentity.planeDescriptors[planeIndex];
        if (leftPlane.planeIndex != rightPlane.planeIndex ||
            leftPlane.fileSystemDevice != rightPlane.fileSystemDevice ||
            leftPlane.fileSystemInode != rightPlane.fileSystemInode ||
            leftPlane.pitchBytes != rightPlane.pitchBytes ||
            leftPlane.offsetBytes != rightPlane.offsetBytes ||
            leftPlane.sizeBytes != rightPlane.sizeBytes) {
            return false;
        }
    }
    return true;
}

void DmaBuffer::buildBufferIdentity() {
    bufferIdentity_ = {};
    bufferIdentity_.width = data_.width;
    bufferIdentity_.height = data_.height;
    bufferIdentity_.format = data_.format;
    bufferIdentity_.modifier = 0;

    DmaBufferPlaneDescriptor planeDescriptor {};
    planeDescriptor.planeIndex = data_.channel;
    planeDescriptor.pitchBytes = data_.pitch;
    planeDescriptor.offsetBytes = data_.offset;
    planeDescriptor.sizeBytes = data_.size;

    // 使用 inode + layout 组合身份, 避免把会被复用的 fd 数字当作对象身份。
    if (!fillFileIdentity(primeFd_, planeDescriptor.fileSystemDevice, planeDescriptor.fileSystemInode)) {
        fprintf(stderr, "[DmaBuffer] Failed to query stable file identity for fd=%d\n", primeFd_);
    }

    bufferIdentity_.planeDescriptors.emplace_back(planeDescriptor);

    uint64_t hashValue = 0;
    hashValue = hashCombine(hashValue, bufferIdentity_.width);
    hashValue = hashCombine(hashValue, bufferIdentity_.height);
    hashValue = hashCombine(hashValue, bufferIdentity_.format);
    hashValue = hashCombine(hashValue, bufferIdentity_.modifier);
    for (const auto& currentPlane : bufferIdentity_.planeDescriptors) {
        hashValue = hashCombine(hashValue, currentPlane.planeIndex);
        hashValue = hashCombine(hashValue, static_cast<uint64_t>(currentPlane.fileSystemDevice));
        hashValue = hashCombine(hashValue, static_cast<uint64_t>(currentPlane.fileSystemInode));
        hashValue = hashCombine(hashValue, currentPlane.pitchBytes);
        hashValue = hashCombine(hashValue, currentPlane.offsetBytes);
        hashValue = hashCombine(hashValue, currentPlane.sizeBytes);
    }
    bufferIdentity_.identityHash = hashValue;
}

// ---------- map/unmap ----------
uint8_t* DmaBuffer::map() {
    if (primeFd_ < 0) {
        throw std::runtime_error("[DmaBuffer] Invalid DMABUF fd");
    }
    if (nullptr != mappedPtr_) {
        return mappedPtr_;
    }
    void* ptr = mmap(nullptr, data_.size, PROT_READ | PROT_WRITE, MAP_SHARED, primeFd_, 0);
    if (MAP_FAILED == ptr) {
        perror("[DmaBuffer] mmap failed");
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
        // owner_ 保持引用不变, 不赋值
    }
    return *this;
}


uint8_t* DmaBuffer::MappedView::get() { return ptr_; }
DmaBuffer::MappedView::operator uint8_t*() { return ptr_; }

// ---------- scopedMap ----------
DmaBuffer::MappedView DmaBuffer::scopedMap() {
    return MappedView(*this, map());
}
