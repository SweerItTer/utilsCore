/*
 * @FilePath: /EdgeVision/include/utils/dma/dmaBuffer.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-12 22:04:45
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef DMA_BUFFER_H
#define DMA_BUFFER_H

#include <memory>
#include <sys/mman.h>
#include <unistd.h>
#include <stdexcept>

#include "drm/deviceController.h"

class DmaBuffer {
struct dmaBufferData {
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
    // ---------- 工厂方法 ----------
    static std::shared_ptr<DmaBuffer> create(uint32_t width, uint32_t height, uint32_t format, uint32_t required_size, uint32_t offset, uint32_t planeIndex);
    static std::shared_ptr<DmaBuffer> create(uint32_t width, uint32_t height, uint32_t format, uint32_t offset, uint32_t planeIndex=0);
    static std::shared_ptr<DmaBuffer> importFromFD(int importFd, uint32_t width, uint32_t height, uint32_t format, uint32_t size, uint32_t offset=0);

    // ---------- 基本信息 ----------
    int fd() const noexcept;
    uint32_t handle() const noexcept;
    uint32_t width()  const noexcept;
    uint32_t height() const noexcept;
    uint32_t format() const noexcept;
    uint32_t pitch()  const noexcept;
    uint32_t size()   const noexcept;
    uint32_t offset() const noexcept;
    uint32_t channel() const noexcept;

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

    // ---------- 构造/析构 ----------
    DmaBuffer(const DmaBuffer&) = delete;
    DmaBuffer& operator=(const DmaBuffer&) = delete;
    DmaBuffer(DmaBuffer&& other) noexcept;
    DmaBuffer& operator=(DmaBuffer&& other) noexcept;
    ~DmaBuffer();

private:
    static int exportFD(drm_mode_create_dumb& create_arg);
    DmaBuffer(int primeFd, dmaBufferData& data);
    DmaBuffer(int primeFd, dmaBufferData& data, bool isimport);
    void cleanup() noexcept;

    int m_fd = -1;
    dmaBufferData data_;
    uint8_t* mappedPtr_ = nullptr;
    std::atomic<bool> isimport_{false};
};

using DmaBufferPtr = std::shared_ptr<DmaBuffer>;

#endif // DMA_BUFFER_H
