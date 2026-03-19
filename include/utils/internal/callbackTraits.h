#ifndef UTILS_INTERNAL_CALLBACK_TRAITS_H
#define UTILS_INTERNAL_CALLBACK_TRAITS_H

#include <type_traits>
#include <utility>

namespace utils {
namespace internal {

// C++14 没有 std::remove_cvref_t,这里补一个轻量版本,避免模板匹配时被引用/const 干扰.
template <typename T>
struct remove_cvref {
    using type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
};

template <typename T>
using remove_cvref_t = typename remove_cvref<T>::type;

template <typename...>
using void_t = void;

// 检查 F(args...) 这个表达式是否成立.
// 这里只判断"能不能调用",不关心返回值是否符合预期.
template <typename F, typename, typename... Args>
struct is_invocable_impl : std::false_type {};

template <typename F, typename... Args>
struct is_invocable_impl<F, void_t<decltype(std::declval<F>()(std::declval<Args>()...))>, Args...>
    : std::true_type {};

template <typename F, typename... Args>
struct is_invocable : is_invocable_impl<F, void, Args...> {};

// 提取调用结果类型.只有在 F(args...) 合法时才会暴露 type.
template <typename F, typename... Args>
struct invoke_result;

template <typename F, typename... Args>
struct invoke_result<F, typename std::enable_if<is_invocable<F, Args...>::value>::type, Args...> {
    using type = decltype(std::declval<F>()(std::declval<Args>()...));
};

template <typename F, typename... Args>
using invoke_result_t = typename invoke_result<F, void, Args...>::type;

// 检查返回值是否"精确匹配" Expected.
// 这里故意不用"可转换即可",因为本项目希望模板误用时尽早在编译期直接失败.
template <typename Expected, typename F, typename = void, typename... Args>
struct is_exact_invocable_r_impl : std::false_type {};

template <typename Expected, typename F, typename... Args>
struct is_exact_invocable_r_impl<
    Expected,
    F,
    typename std::enable_if<is_invocable<F, Args...>::value>::type,
    Args...>
    : std::is_same<remove_cvref_t<Expected>, remove_cvref_t<invoke_result_t<F, Args...>>> {};

template <typename Expected, typename F, typename... Args>
struct is_exact_invocable_r : is_exact_invocable_r_impl<Expected, F, void, Args...> {};

} // namespace internal
} // namespace utils

#endif // UTILS_INTERNAL_CALLBACK_TRAITS_H
