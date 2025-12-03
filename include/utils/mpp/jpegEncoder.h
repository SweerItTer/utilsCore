/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-01 16:34:58
 * @FilePath: /EdgeVision/include/utils/mpp/jpegEncoder.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include <memory>

#include "mpp/encoderContext.h"
#include "dma/dmaBuffer.h"

class JpegEncoder {
public:
    struct Config {
        uint32_t width;
        uint32_t height;
        MppFrameFormat format = MPP_FMT_YUV420SP;
        int quality = 8;  // 0-10, 值越大质量越高
        std::string save_dir;
        // 转换为 MPP 配置
        MppEncoderContext::Config toMppConfig() const {
            return DefaultConfigs::createJpegConfig(
                width, height, format, quality
            );
        }
    };
    
    explicit JpegEncoder(const Config& cfg);
    ~JpegEncoder() = default;
    
    // 重新配置
    bool resetConfig(const Config& cfg);
    
    bool captureFromDmabuf(const DmaBufferPtr dmabuf);
    
private:
    bool encodeToFile(MppFrame frame, const std::string& filepath);
    std::string generateFilename();
    
    Config config_;
    std::atomic_bool initialized_{false};
    std::unique_ptr<MppEncoderContext> encoder_ctx_;
    MppApi* mpi{nullptr};
    MppCtx ctx{nullptr};
};