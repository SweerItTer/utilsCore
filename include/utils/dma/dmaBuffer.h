/*
 * @FilePath: /EdgeVision/include/utils/dma/dmaBuffer.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-12 22:04:45
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef DMA_BUFFER_H
#define DMA_BUFFER_H

#include <memory>

#include "drm/deviceController.h"

class DmaBuffer {
public:    
    // 根据实际 size 尝试通过修改分辨率实现逼近
    static std::shared_ptr<DmaBuffer> create(uint32_t width, uint32_t height, uint32_t format, uint32_t required_size);

    static std::shared_ptr<DmaBuffer> create(uint32_t width, uint32_t height, uint32_t format);

    static std::shared_ptr<DmaBuffer> importFromFD(int importFd, uint32_t width, uint32_t height, uint32_t format);

    uint32_t handle() const noexcept { return m_handle; }
    uint32_t width()  const noexcept { return m_width;  }
    uint32_t height() const noexcept { return m_height; }
    uint32_t format() const noexcept { return m_format; }
    uint32_t pitch()  const noexcept { return m_pitch;  }
    uint32_t size()   const noexcept { return m_size;   }

    int fd() const noexcept { return m_fd; }

    DmaBuffer(const DmaBuffer&) = delete;
    DmaBuffer& operator=(const DmaBuffer&) = delete;

    DmaBuffer(DmaBuffer&& other) noexcept;
    DmaBuffer& operator=(DmaBuffer&& other) noexcept;
    ~DmaBuffer();
private:
    static int exportFD(drm_mode_create_dumb& create_arg);

    DmaBuffer(int prime_fd, uint32_t handle, uint32_t width,
        uint32_t height, uint32_t format, uint32_t pitch, uint32_t size);

    void cleanup() noexcept;
    
    int m_fd = -1;
    uint32_t m_handle  = 0;
    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    uint32_t m_format = 0;
    uint32_t m_pitch  = 0;
    uint32_t m_size   = 0;
};

using DmaBufferPtr = std::shared_ptr<DmaBuffer>;

#endif // DMA_BUFFER_H
