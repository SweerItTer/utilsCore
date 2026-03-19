/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-03-14 00:00:00
 * @FilePath: /include/utils/drm/framebufferCache.h
 * @LastEditors: OpenAI Codex
 */
#ifndef FRAMEBUFFER_CACHE_H
#define FRAMEBUFFER_CACHE_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "dma/dmaBuffer.h"

/**
 * @brief 将一组 DMA-BUF 导入结果缓存为可复用的 DRM framebuffer.
 *
 * 缓存命中使用两级判断:
 * 1. generation + 预计算 hash 做快路径查找
 * 2. 完整 BufferIdentity 做慢路径确认
 *
 * 这样可以避免热路径每次重新拼装复杂键,同时在热插拔后通过 generation
 * 明确隔离旧的 framebuffer,避免复用失效资源.
 */
class FramebufferCache {
public:
    /**
     * @brief 一组 plane 的聚合身份.
     */
    struct FramebufferIdentity {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t format = 0;
        std::vector<DmaBuffer::BufferIdentity> planeIdentities;
        uint64_t identityHash = 0;
    };

    /**
     * @brief 返回给调用方的 framebuffer 租约句柄.
     *
     * 句柄只保存轻量索引信息,真正的 entry 仍由缓存所有.
     */
    struct FramebufferHandle {
        uint32_t framebufferId = 0;
        uint64_t generation = 0;
        uint64_t identityHash = 0;

        bool valid() const noexcept {
            return framebufferId != 0;
        }
    };

    FramebufferCache() = default;
    ~FramebufferCache();

    /**
     * @brief 获取可复用 framebuffer,必要时创建新的 framebuffer.
     * @param dmaBuffers 当前图层使用的 DMA-BUF planes
     * @param width framebuffer 宽度
     * @param height framebuffer 高度
     * @param format framebuffer DRM 格式
     * @param generation 当前 DRM 资源代际
     * @return FramebufferHandle 可用于当前提交的 framebuffer 句柄
     */
    FramebufferHandle acquireFramebuffer(const std::vector<DmaBufferPtr>& dmaBuffers,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t format,
                                         uint64_t generation);
    /**
     * @brief 释放一次 framebuffer 使用租约.
     * @param framebufferHandle 由 acquireFramebuffer 返回的句柄
     */
    void releaseFramebuffer(const FramebufferHandle& framebufferHandle);
    /**
     * @brief 清理非当前代际的 framebuffer.
     * @param currentGeneration 当前仍允许复用的代际
     */
    void clearGenerationCache(uint64_t currentGeneration);
    /**
     * @brief 清理全部缓存.
     */
    void clearAllCache();

private:
    struct FramebufferEntry {
        uint32_t framebufferId = 0;
        uint64_t generation = 0;
        FramebufferIdentity identity;
        std::size_t activeLeaseCount = 0;
        uint64_t lastUseSerial = 0;
    };

    FramebufferIdentity buildFramebufferIdentity(const std::vector<DmaBufferPtr>& dmaBuffers,
                                                 uint32_t width,
                                                 uint32_t height,
                                                 uint32_t format) const;
    bool sameFramebufferIdentity(const FramebufferIdentity& leftIdentity,
                                 const FramebufferIdentity& rightIdentity) const;
    uint32_t createFramebuffer(const std::vector<DmaBufferPtr>& dmaBuffers,
                               uint32_t width,
                               uint32_t height,
                               uint32_t format) const;
    void destroyFramebuffer(uint32_t& framebufferId) const noexcept;
    void garbageCollectLocked(uint64_t currentGeneration);

    std::unordered_map<uint64_t, std::vector<std::shared_ptr<FramebufferEntry>>> entriesByHash_;
    std::mutex cacheMutex_;
    uint64_t serialCounter_ = 0;
};

using FramebufferCachePtr = std::shared_ptr<FramebufferCache>;

#endif // FRAMEBUFFER_CACHE_H
