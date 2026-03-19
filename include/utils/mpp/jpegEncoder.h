/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-01 16:34:58
 * @FilePath: /include/utils/mpp/jpegEncoder.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include <memory>
#include <atomic>
#include <string>

#include "mpp/encoderContext.h"
#include "dma/dmaBuffer.h"

/**
 * @brief 基于 Rockchip MPP 的 JPEG 单帧抓拍器
 *
 * 该类面向"把现有 DMABUF 抓拍成 JPEG 文件"的场景.调用方负责保证传入的 DMABUF
 * 生命周期在 `captureFromDmabuf()` 返回前有效, 并且像素格式与配置中的 `format` 匹配.
 */
class JpegEncoder {
public:
    /**
     * @brief JPEG 编码配置
     */
    struct Config {
        uint32_t width;                             ///< 输入图像宽度
        uint32_t height;                            ///< 输入图像高度
        MppFrameFormat format = MPP_FMT_YUV420SP;   ///< 输入格式, 默认 NV12
        int quality = 8;                            ///< 0-10, 值越大质量越高
        std::string save_dir;                       ///< JPEG 输出目录
        int packet_poll_retries = 400;             ///< `encode_get_packet()` 最大轮询次数
        int packet_poll_interval_us = 2000;        ///< JPEG 轮询间隔, 单位微秒
        int packet_ready_timeout_us = 800000;      ///< JPEG 总等待超时, 单位微秒

        /**
         * @brief 转换为通用 MPP 编码配置
         * @return 适用于 `MppEncoderContext` 的 JPEG 配置
         */
        MppEncoderContext::Config toMppConfig() const {
            auto config = DefaultConfigs::createJpegConfig(
                width, height, format, quality
            );
            config.packet_poll_retries = packet_poll_retries;
            config.packet_poll_interval_us = packet_poll_interval_us;
            config.packet_ready_timeout_us = packet_ready_timeout_us;
            return config;
        }
    };
    
    explicit JpegEncoder(const Config& cfg);
    ~JpegEncoder() = default;
    
    /**
     * @brief 重新配置 JPEG 编码器
     * @param cfg 新配置
     * @return true 配置已成功应用
     * @return false 配置非法或 MPP 重置失败
     */
    bool resetConfig(const Config& cfg);
    
    /**
     * @brief 从 DMABUF 直接抓拍并写入 JPEG 文件
     * @param dmabuf 待编码的 DMABUF
     * @return true 抓拍成功
     * @return false 编码或写文件失败
     */
    bool captureFromDmabuf(const DmaBufferPtr dmabuf);
    
private:
    /**
     * @brief 把一帧 MPP frame 编码并保存到文件
     * @param frame 已设置完成的 MPP 帧
     * @param filepath 输出文件路径
     * @return true 写入成功
     * @return false 编码超时或文件写入失败
     */
    bool encodeToFile(MppFrame frame, const std::string& filepath);

    /**
     * @brief 生成带时间戳的 JPEG 文件名
     * @return 完整输出路径
     */
    std::string generateFilename();
    
    Config config_;
    std::atomic_bool initialized_{false};
    std::unique_ptr<MppEncoderContext> encoder_ctx_;
    MppApi* mpi{nullptr};
    MppCtx ctx{nullptr};
};
