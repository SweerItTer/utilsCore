#ifndef UTILS_INTERNAL_STATIC_CALLBACK_H
#define UTILS_INTERNAL_STATIC_CALLBACK_H

#include <memory>
#include <type_traits>
#include <utility>

#include "internal/callbackTraits.h"

namespace utils {
namespace internal {

template <typename Signature>
class StaticCallback;

// 这是一个"轻量,热点友好"的回调槽:
// 1. 对普通 lambda / 函数对象:只在绑定时做一次模板实例化和可选堆分配;
// 2. 对成员函数:保存对象指针 + 编译期确定的 thunk,调用路径不再经过 std::function.
// 目标不是完全复刻 std::function,而是覆盖当前热点所需的可调用场景.
template <typename R, typename... Args>
class StaticCallback<R(Args...)> {
public:
    StaticCallback() noexcept = default;
    StaticCallback(std::nullptr_t) noexcept {}

    template <
        typename Callable,
        typename Decayed = typename std::decay<Callable>::type,
        typename std::enable_if<!std::is_same<Decayed, StaticCallback>::value, int>::type = 0>
    StaticCallback(Callable&& callable) {
        bind(std::forward<Callable>(callable));
    }

    template <
        typename Callable,
        typename Decayed = typename std::decay<Callable>::type,
        typename std::enable_if<!std::is_same<Decayed, StaticCallback>::value, int>::type = 0>
    void bind(Callable&& callable) {
        // 参数和返回值都在这里做编译期检查.
        // 一旦签名不匹配,报错会尽量靠近调用点,而不是等到运行时才发现.
        static_assert(
            is_invocable<Decayed&, Args...>::value,
            "Callback must be invocable with the configured argument types");
        static_assert(
            is_exact_invocable_r<R, Decayed&, Args...>::value,
            "Callback return type must exactly match the configured signature");

        auto holder = std::make_shared<Decayed>(std::forward<Callable>(callable));
        holder_ = holder;
        context_ = holder.get();
        // invoke_ 是一个普通函数指针,后续热路径调用只需要一次间接跳转.
        invoke_ = &invokeCallable<Decayed>;
    }

    // 成员函数绑定路径不需要额外分配 callable 对象.
    // 对 CameraController / RgaProcessor 这类长期存活对象,适合直接绑定到成员函数.
    template <typename Owner, R (Owner::*Method)(Args...)>
    static StaticCallback bindMember(Owner* owner) noexcept {
        StaticCallback callback;
        callback.context_ = owner;
        callback.invoke_ = &invokeMember<Owner, Method>;
        return callback;
    }

    template <typename Owner, R (Owner::*Method)(Args...) const>
    static StaticCallback bindMember(const Owner* owner) noexcept {
        StaticCallback callback;
        callback.context_ = const_cast<Owner*>(owner);
        callback.invoke_ = &invokeConstMember<Owner, Method>;
        return callback;
    }

    template <typename Owner, R (Owner::*Method)(Args...)>
    static StaticCallback bindShared(const std::shared_ptr<Owner>& owner) noexcept {
        StaticCallback callback;
        // bindShared 用于"对象必须被回调延长生命周期"的场景.
        // 例如 packaged_task 放进队列后,执行前不能提前析构.
        callback.holder_ = owner;
        callback.context_ = owner.get();
        callback.invoke_ = &invokeMember<Owner, Method>;
        return callback;
    }

    template <typename Owner, R (Owner::*Method)(Args...) const>
    static StaticCallback bindShared(const std::shared_ptr<const Owner>& owner) noexcept {
        StaticCallback callback;
        callback.holder_ = owner;
        callback.context_ = const_cast<Owner*>(owner.get());
        callback.invoke_ = &invokeConstMember<Owner, Method>;
        return callback;
    }

    explicit operator bool() const noexcept {
        return invoke_ != nullptr;
    }

    void reset() noexcept {
        holder_.reset();
        context_ = nullptr;
        invoke_ = nullptr;
    }

    R operator()(Args... args) const {
        // 调用方应先通过 bool 判断是否已绑定;这里保持热路径最短,不做额外防御分支.
        return invoke_(context_, std::forward<Args>(args)...);
    }

private:
    using InvokeFn = R (*)(void*, Args...);

    template <typename Callable>
    static R invokeCallable(void* context, Args... args) {
        return (*static_cast<Callable*>(context))(std::forward<Args>(args)...);
    }

    template <typename Owner, R (Owner::*Method)(Args...)>
    static R invokeMember(void* context, Args... args) {
        return (static_cast<Owner*>(context)->*Method)(std::forward<Args>(args)...);
    }

    template <typename Owner, R (Owner::*Method)(Args...) const>
    static R invokeConstMember(void* context, Args... args) {
        return (static_cast<const Owner*>(context)->*Method)(std::forward<Args>(args)...);
    }

    // holder_ 只在需要拥有 callable 生命周期时使用.
    // 对 bindMember 这种"外部对象负责存活"的路径,它会保持为空.
    std::shared_ptr<void> holder_;
    void* context_ = nullptr;
    InvokeFn invoke_ = nullptr;
};

} // namespace internal
} // namespace utils

#endif // UTILS_INTERNAL_STATIC_CALLBACK_H
