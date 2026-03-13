/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-03-14 00:00:00
 * @FilePath: /src/utils/drm/framebufferCache.cpp
 * @LastEditors: OpenAI Codex
 */
#include "drm/framebufferCache.h"

#include <cerrno>

#include "drm/deviceController.h"

namespace {

uint64_t hashCombine(uint64_t seed, uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
}

} // namespace

FramebufferCache::~FramebufferCache() {
    clearAllCache();
}

FramebufferCache::FramebufferHandle FramebufferCache::acquireFramebuffer(
    const std::vector<DmaBufferPtr>& dmaBuffers,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint64_t generation) {
    if (dmaBuffers.empty()) {
        return {};
    }

    const FramebufferIdentity framebufferIdentity = buildFramebufferIdentity(
        dmaBuffers, width, height, format);

    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto& candidates = entriesByHash_[framebufferIdentity.identityHash];
    for (const auto& currentEntry : candidates) {
        if (!currentEntry || currentEntry->generation != generation) {
            continue;
        }
        if (!sameFramebufferIdentity(currentEntry->identity, framebufferIdentity)) {
            continue;
        }

        currentEntry->activeLeaseCount += 1U;
        currentEntry->lastUseSerial = ++serialCounter_;
        return {currentEntry->framebufferId, currentEntry->generation, framebufferIdentity.identityHash};
    }

    uint32_t framebufferId = createFramebuffer(dmaBuffers, width, height, format);
    if (framebufferId == 0) {
        return {};
    }

    auto newEntry = std::make_shared<FramebufferEntry>();
    newEntry->framebufferId = framebufferId;
    newEntry->generation = generation;
    newEntry->identity = framebufferIdentity;
    newEntry->activeLeaseCount = 1U;
    newEntry->lastUseSerial = ++serialCounter_;
    candidates.emplace_back(std::move(newEntry));
    garbageCollectLocked(generation);

    return {framebufferId, generation, framebufferIdentity.identityHash};
}

void FramebufferCache::releaseFramebuffer(const FramebufferHandle& framebufferHandle) {
    if (!framebufferHandle.valid()) {
        return;
    }

    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto mapIt = entriesByHash_.find(framebufferHandle.identityHash);
    if (mapIt == entriesByHash_.end()) {
        return;
    }

    for (const auto& currentEntry : mapIt->second) {
        if (!currentEntry ||
            currentEntry->generation != framebufferHandle.generation ||
            currentEntry->framebufferId != framebufferHandle.framebufferId) {
            continue;
        }
        if (currentEntry->activeLeaseCount > 0U) {
            currentEntry->activeLeaseCount -= 1U;
        }
        break;
    }
}

void FramebufferCache::clearGenerationCache(uint64_t currentGeneration) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    garbageCollectLocked(currentGeneration);
}

void FramebufferCache::clearAllCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    for (auto& bucketPair : entriesByHash_) {
        for (auto& currentEntry : bucketPair.second) {
            if (!currentEntry) {
                continue;
            }
            destroyFramebuffer(currentEntry->framebufferId);
        }
    }
    entriesByHash_.clear();
}

FramebufferCache::FramebufferIdentity FramebufferCache::buildFramebufferIdentity(
    const std::vector<DmaBufferPtr>& dmaBuffers,
    uint32_t width,
    uint32_t height,
    uint32_t format) const {
    FramebufferIdentity framebufferIdentity {};
    framebufferIdentity.width = width;
    framebufferIdentity.height = height;
    framebufferIdentity.format = format;

    uint64_t hashValue = 0;
    hashValue = hashCombine(hashValue, width);
    hashValue = hashCombine(hashValue, height);
    hashValue = hashCombine(hashValue, format);
    for (const auto& currentBuffer : dmaBuffers) {
        if (!currentBuffer) {
            continue;
        }
        framebufferIdentity.planeIdentities.emplace_back(currentBuffer->identity());
        hashValue = hashCombine(hashValue, currentBuffer->identityHash());
    }
    framebufferIdentity.identityHash = hashValue;
    return framebufferIdentity;
}

bool FramebufferCache::sameFramebufferIdentity(const FramebufferIdentity& leftIdentity,
                                               const FramebufferIdentity& rightIdentity) const {
    if (leftIdentity.width != rightIdentity.width ||
        leftIdentity.height != rightIdentity.height ||
        leftIdentity.format != rightIdentity.format ||
        leftIdentity.planeIdentities.size() != rightIdentity.planeIdentities.size()) {
        return false;
    }

    for (std::size_t planeIndex = 0; planeIndex < leftIdentity.planeIdentities.size(); ++planeIndex) {
        const auto& leftPlaneIdentity = leftIdentity.planeIdentities[planeIndex];
        const auto& rightPlaneIdentity = rightIdentity.planeIdentities[planeIndex];
        if (leftPlaneIdentity.width != rightPlaneIdentity.width ||
            leftPlaneIdentity.height != rightPlaneIdentity.height ||
            leftPlaneIdentity.format != rightPlaneIdentity.format ||
            leftPlaneIdentity.modifier != rightPlaneIdentity.modifier ||
            leftPlaneIdentity.planeDescriptors.size() != rightPlaneIdentity.planeDescriptors.size()) {
            return false;
        }
        for (std::size_t descriptorIndex = 0;
             descriptorIndex < leftPlaneIdentity.planeDescriptors.size();
             ++descriptorIndex) {
            const auto& leftDescriptor = leftPlaneIdentity.planeDescriptors[descriptorIndex];
            const auto& rightDescriptor = rightPlaneIdentity.planeDescriptors[descriptorIndex];
            if (leftDescriptor.planeIndex != rightDescriptor.planeIndex ||
                leftDescriptor.fileSystemDevice != rightDescriptor.fileSystemDevice ||
                leftDescriptor.fileSystemInode != rightDescriptor.fileSystemInode ||
                leftDescriptor.pitchBytes != rightDescriptor.pitchBytes ||
                leftDescriptor.offsetBytes != rightDescriptor.offsetBytes ||
                leftDescriptor.sizeBytes != rightDescriptor.sizeBytes) {
                return false;
            }
        }
    }

    return true;
}

uint32_t FramebufferCache::createFramebuffer(const std::vector<DmaBufferPtr>& dmaBuffers,
                                             uint32_t width,
                                             uint32_t height,
                                             uint32_t format) const {
    uint32_t handles[4] = {0};
    uint32_t pitches[4] = {0};
    uint32_t offsets[4] = {0};

    for (std::size_t planeIndex = 0; planeIndex < dmaBuffers.size(); ++planeIndex) {
        const auto& currentBuffer = dmaBuffers[planeIndex];
        if (!currentBuffer) {
            fprintf(stderr, "[FramebufferCache] Invalid DmaBuffer on plane %zu\n", planeIndex);
            return 0;
        }
        handles[planeIndex] = currentBuffer->handle();
        pitches[planeIndex] = currentBuffer->pitch();
        offsets[planeIndex] = currentBuffer->offset();
    }

    uint32_t framebufferId = 0;
    std::lock_guard<std::mutex> lock(DrmDev::fd_mutex);
    const int ret = drmModeAddFB2(
        DrmDev::fd_ptr->get(),
        width,
        height,
        format,
        handles,
        pitches,
        offsets,
        &framebufferId,
        0);
    if (ret != 0 || framebufferId == 0) {
        fprintf(stderr, "[FramebufferCache] drmModeAddFB2 failed ret=%d\n", ret);
        return 0;
    }
    return framebufferId;
}

void FramebufferCache::destroyFramebuffer(uint32_t& framebufferId) const noexcept {
    if (framebufferId == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(DrmDev::fd_mutex);
    if (!DrmDev::fd_ptr || DrmDev::fd_ptr->get() < 0) {
        framebufferId = 0;
        return;
    }

    const int ret = drmModeRmFB(DrmDev::fd_ptr->get(), framebufferId);
    if (ret < 0) {
        fprintf(stderr, "[FramebufferCache] drmModeRmFB failed errno=%d\n", errno);
    }
    framebufferId = 0;
}

void FramebufferCache::garbageCollectLocked(uint64_t currentGeneration) {
    for (auto mapIt = entriesByHash_.begin(); mapIt != entriesByHash_.end();) {
        auto& entries = mapIt->second;
        for (auto entryIt = entries.begin(); entryIt != entries.end();) {
            const auto& currentEntry = *entryIt;
            const bool generationMismatch = currentEntry && currentEntry->generation != currentGeneration;
            const bool idleEntry = currentEntry && currentEntry->activeLeaseCount == 0U;
            const bool overCapacity = entries.size() > 8U;
            if (currentEntry && idleEntry && (generationMismatch || overCapacity)) {
                destroyFramebuffer(currentEntry->framebufferId);
                entryIt = entries.erase(entryIt);
                continue;
            }
            ++entryIt;
        }
        if (entries.empty()) {
            mapIt = entriesByHash_.erase(mapIt);
            continue;
        }
        ++mapIt;
    }
}
