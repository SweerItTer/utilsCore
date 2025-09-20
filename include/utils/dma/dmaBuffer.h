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
};

public:    
    // 根据实际 size 尝试通过修改分辨率实现逼近
    static std::shared_ptr<DmaBuffer> create(uint32_t width, uint32_t height, uint32_t format, uint32_t required_size, uint32_t offset);

    static std::shared_ptr<DmaBuffer> create(uint32_t width, uint32_t height, uint32_t format, uint32_t offset);

    static std::shared_ptr<DmaBuffer> importFromFD(int importFd, uint32_t width, uint32_t height, uint32_t format, uint32_t offset);

    int fd() const noexcept { return m_fd; }

    uint32_t handle() const noexcept { return data_.handle; }
    uint32_t width()  const noexcept { return data_.width;  }
    uint32_t height() const noexcept { return data_.height; }
    uint32_t format() const noexcept { return data_.format; }
    uint32_t pitch()  const noexcept { return data_.pitch;  }
    uint32_t size()   const noexcept { return data_.size;   }
    uint32_t offset() const noexcept { return data_.offset; }

    // map 返回可写的 CPU 指针
    uint8_t* map() {
        if (m_fd < 0) {
            throw std::runtime_error("Invalid DMABUF fd");
        }
        if (mappedPtr_) {
            return mappedPtr_;
        }

        void* ptr = mmap(nullptr, data_.size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
        if (ptr == MAP_FAILED) {
            perror("mmap failed");
            return nullptr;
        }
        mappedPtr_ = reinterpret_cast<uint8_t*>(ptr);
        return mappedPtr_;
    }

    void unmap() {
        if (mappedPtr_) {
            munmap(mappedPtr_, data_.size);
            mappedPtr_ = nullptr;
        }
    }
    
    // 禁止拷贝
    DmaBuffer(const DmaBuffer&) = delete;
    DmaBuffer& operator=(const DmaBuffer&) = delete;

    DmaBuffer(DmaBuffer&& other) noexcept;
    DmaBuffer& operator=(DmaBuffer&& other) noexcept;
    ~DmaBuffer();
private:
    static int exportFD(drm_mode_create_dumb& create_arg);
    
    DmaBuffer(int primeFd, dmaBufferData& data);

    void cleanup() noexcept;
    
    int m_fd = -1;
    dmaBufferData data_;
    uint8_t* mappedPtr_ = nullptr;
};

using DmaBufferPtr = std::shared_ptr<DmaBuffer>;

#endif // DMA_BUFFER_H
