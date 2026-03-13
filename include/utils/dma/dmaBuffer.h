/*
 * @FilePath: /include/utils/dma/dmaBuffer.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-12 22:04:45
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef DMA_BUFFER_H
#define DMA_BUFFER_H

#include <cstdint>
#include <memory>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdexcept>
#include <vector>

class DmaBuffer {
public:
    /**
     * @brief 单个 DMA-BUF plane 的稳定身份描述。
     * @param planeIndex plane 序号
     * @param fileSystemDevice `fstat(fd)` 返回的 st_dev
     * @param fileSystemInode `fstat(fd)` 返回的 st_ino
     * @param pitchBytes 当前 plane 的 stride/pitch
     * @param offsetBytes 当前 plane 的数据偏移
     * @param sizeBytes 当前 plane 的字节大小
     */
    struct DmaBufferPlaneDescriptor {
        uint32_t planeIndex = 0;
        dev_t fileSystemDevice = 0;
        ino_t fileSystemInode = 0;
        uint32_t pitchBytes = 0;
        uint32_t offsetBytes = 0;
        uint64_t sizeBytes = 0;
    };

    /**
     * @brief DmaBuffer 的稳定身份。
     *
     * 这里缓存的是“内核对象身份 + 布局信息”，不是用户态 fd 数字。
     * fd 可以被内核复用，但 inode/layout 组合在正常生命周期内更稳定，
     * 因而适合用于 framebuffer 复用的慢路径校验。
     */
    struct BufferIdentity {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t format = 0;
        uint64_t modifier = 0;
        std::vector<DmaBufferPlaneDescriptor> planeDescriptors;
        uint64_t identityHash = 0;
    };

private:
struct DmaBufferData {
    uint32_t handle;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t pitch;
    uint64_t size;
    uint32_t offset;
    uint32_t channel;
};

public:
    // ---------- 创建方法 ----------
    // 指定大小的 dumb buffer 创建
    static std::shared_ptr<DmaBuffer> create(uint32_t width, uint32_t height, uint32_t format, uint32_t requiredSize, uint32_t offset, uint32_t planeIndex);
    // 自动计算大小的 dumb buffer 创建
    static std::shared_ptr<DmaBuffer> create(uint32_t width, uint32_t height, uint32_t format, uint32_t offset, uint32_t planeIndex=0);
    // 导入外部 dma-buf fd
    static std::shared_ptr<DmaBuffer> importFromFD(int importFd, uint32_t width, uint32_t height, uint32_t format, uint32_t size, uint32_t offset=0);

    // ---------- 基本信息 ----------
    const int fd() const noexcept;
    const uint32_t handle() const noexcept;
    const uint32_t width()  const noexcept;
    const uint32_t height() const noexcept;
    const uint32_t format() const noexcept;
    const uint32_t pitch()  const noexcept;
    const uint32_t size()   const noexcept;  // 保持API兼容性, 返回uint32_t
    const uint64_t size64() const noexcept;  // 新增方法, 返回完整的uint64_t大小
    const uint32_t offset() const noexcept;
    const uint32_t channel() const noexcept;
    /**
     * @brief 获取预计算好的稳定身份。
     * @return const BufferIdentity& 当前 buffer 的身份快照
     */
    const BufferIdentity& identity() const noexcept;
    /**
     * @brief 获取身份哈希值, 用于热路径缓存命中。
     * @return uint64_t 预计算的身份哈希
     */
    uint64_t identityHash() const noexcept;
    /**
     * @brief 比较两个 DmaBuffer 的完整稳定身份。
     * @param other 需要比较的另一个 DmaBuffer
     * @return true 完整身份一致
     * @return false 任意关键字段不一致
     */
    bool sameIdentity(const DmaBuffer& other) const noexcept;

    // ---------- 映射控制 ----------
    uint8_t* map();
    void unmap();

    // ---------- RAII 封装 ----------
    class MappedView {
    public:
        MappedView(DmaBuffer& owner, uint8_t* ptr);
        ~MappedView();
        MappedView(const MappedView&) = delete;
        MappedView& operator=(const MappedView&) = delete;
        MappedView(MappedView&& other) noexcept;
        MappedView& operator=(MappedView&& other) noexcept;

        uint8_t* get();
        operator uint8_t*();

    private:
        DmaBuffer& owner_;
        uint8_t* ptr_;
    };

    // 获取一个作用域内自动 unmap 的视图
    MappedView scopedMap();

    // ---------- 禁止拷贝/允许移动 ----------
    DmaBuffer(const DmaBuffer&) = delete;
    DmaBuffer& operator=(const DmaBuffer&) = delete;
    DmaBuffer(DmaBuffer&& other) noexcept;
    DmaBuffer& operator=(DmaBuffer&& other) noexcept;

    // ---------- 构造/析构 ----------
    ~DmaBuffer();
private:
    DmaBuffer(int primeFd, DmaBufferData& data, bool isImported);
    // 资源清理
    void cleanup() noexcept;
    // 在创建/导入成功后预计算稳定身份, 避免热路径重复查询 plane/layout 信息。
    void buildBufferIdentity();
    // 尝试创建 dumb buffer 辅助函数
    static bool tryCreateDumbBuffer(DmaBufferData &data, uint32_t requiredSize, uint32_t planeIndex,
        float ratioW, float ratioH, uint32_t bpp);
    
    int primeFd_ = -1;
    DmaBufferData data_{};
    uint8_t* mappedPtr_ = nullptr;
    bool isImported_{false};
    BufferIdentity bufferIdentity_{};
};

// 智能指针类型定义
using DmaBufferPtr = std::shared_ptr<DmaBuffer>;

#endif // DMA_BUFFER_H
