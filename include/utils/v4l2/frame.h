/*
 * @FilePath: /include/utils/v4l2/frame.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-05 01:09:32
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */

#ifndef FRAME_H
#define FRAME_H

#include <cstdint>
#include <cstddef>
#include <new>
#include <utility>
#include "v4l2/v4l2Exception.h"
#include "sharedBufferState.h"
#include "fixedSizePool.h"
#include "internal/staticCallback.h"

struct FrameMeta {
    uint64_t    frame_id = -1;      // 单调递增
    uint64_t    timestamp_ns = -1;  // CLOCK_MONOTONIC
    int         index = -1;         // buffer index
    // 原始尺寸(便于放大 box)
    uint32_t    w = 0;
    uint32_t    h = 0;
};

// 统一帧接口(MMAP&DMABUF)
class Frame {
    static FixedSizePool s_pool_;
public:
    // 对外仍然保留"注册释放回调"的用法,但内部不再持有 std::function.
    // 这样析构释放路径可以走更轻的静态 thunk,而不是类型擦除调用.
    using ReleaseCallback = utils::internal::StaticCallback<void(int)>;
    using SharedBufferPtr = std::shared_ptr<SharedBufferState>;
    FrameMeta meta;
    // 强类型枚举
    enum class MemoryType { Unknown, MMAP, DMABUF };

    Frame() noexcept;
    ~Frame();
    Frame(SharedBufferPtr s);
    Frame(std::vector<SharedBufferPtr> states);

    static void* operator new(std::size_t size);

    static void operator delete(void* p) noexcept;
    
    MemoryType type() const noexcept { return type_; }
    
    // 二次检查
    void* data(int planeIndex = -1) const;
    int dmabuf_fd(int planeIndex = -1) const;

    size_t size() const;
    uint64_t timestamp() const { return meta.timestamp_ns; }
    SharedBufferPtr sharedState(int planeIndex = -1) const noexcept {
        if (!mutiPlane_) {
            return state_;
        }
        if (planeIndex < 0 || planeIndex >= states_.size()) {
            fprintf(stderr, "Frame is mutiplane.\n");
            return nullptr;
        }
        auto s = states_[planeIndex];
        if (nullptr == s || false == s->valid) {
            fprintf(stderr, "Current Plane is invalid.\n");
            return nullptr;
        }
        return s;
    }
    int index() const { return meta.index; }

    void setTimestamp(uint64_t ts) { meta.timestamp_ns = ts; }

    // 已经是内部静态回调槽时,直接接管所有权.
    void setReleaseCallback(ReleaseCallback bufReleasCallback) {
        bufReleasCallback_ = std::move(bufReleasCallback);
    }

    template <
        typename Callable,
        typename Decayed = typename std::decay<Callable>::type,
        typename std::enable_if<!std::is_same<Decayed, ReleaseCallback>::value, int>::type = 0>
    // 兼容普通 lambda / 函数对象调用方式.
    // 调用方的写法尽量不变,但这里会在编译期检查签名是否真的是 void(int).
    void setReleaseCallback(Callable&& bufReleasCallback) {
        bufReleasCallback_ = ReleaseCallback(std::forward<Callable>(bufReleasCallback));
    }

    // 热点路径优先走成员函数绑定,避免每次都构造捕获 lambda.
    template <typename Owner, void (Owner::*Method)(int)>
    void setReleaseCallback(Owner* owner) {
        bufReleasCallback_ = ReleaseCallback::template bindMember<Owner, Method>(owner);
    }
private:
    ReleaseCallback bufReleasCallback_;
    std::atomic_bool mutiPlane_;
    MemoryType type_;
    SharedBufferPtr state_ = nullptr;
    std::vector<SharedBufferPtr> states_;
};

#endif // !FRAME_H
