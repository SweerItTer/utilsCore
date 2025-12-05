#ifndef SIMPLE_VARIANT_H
#define SIMPLE_VARIANT_H

#include <type_traits>
#include <stdexcept>
#include <utility>

// 判断类型列表里是否包含某个类型
template <typename T, typename... Rest>
struct IsIn;

template <typename T>
struct IsIn<T> : std::false_type {};

template <typename T, typename First, typename... Rest>
struct IsIn<T, First, Rest...> : std::conditional_t<
    std::is_same<T, First>::value,
    std::true_type,
    IsIn<T, Rest...>
> {};

// 检查类型列表里是否有重复
template <typename... Types>
struct HasDuplicates;

template <>
struct HasDuplicates<> : std::false_type {};

template <typename First, typename... Rest>
struct HasDuplicates<First, Rest...> : std::conditional_t<
    IsIn<First, Rest...>::value,
    std::true_type,
    HasDuplicates<Rest...>
> {};


// 递归存储
template <typename TFirst, typename... TRest>
struct VariantStorage {
    TFirst first;
    VariantStorage<TRest...> rest;
};
// 递归展开的中止特化
template <typename TLast>
struct VariantStorage<TLast> {
    TLast first;
};

template <typename... Types>
class SimpleVariant {
private:
    static_assert(!HasDuplicates<Types...>::value, "SimpleVariant does not allow duplicate types");

    using Storage = VariantStorage<Types...>;
    Storage storage_;
    int typeIndex_ = -1;

public:
    SimpleVariant() = default;

    template <typename T>
    SimpleVariant(T val) {
        set(val);
    }

    template <typename T>
    void set(T val) {
        typeIndex_ = assign<T, Types...>(val, &storage_);
    }

    template <typename T>
    T get() const {
        return getImpl<T, Types...>(&storage_);
    }

    int index() const { return typeIndex_; }

private:
    // assign递归
    template <typename T, typename TFirst, typename... TRest>
    int assign(T val, VariantStorage<TFirst, TRest...>* s, int currentIndex = 0) {
        if (std::is_same<T, TFirst>::value) {
            s->first = val;
            return currentIndex;
        } else {
            return assign<T, TRest...>(val, &s->rest, currentIndex + 1);
        }
    }

    // assign终止条件(最后一个类型)
    template <typename T, typename TLast>
    int assign(T val, VariantStorage<TLast>* s, int currentIndex = 0) {
        if (std::is_same<T, TLast>::value) {
            s->first = val;
            return currentIndex;
        } else {
            throw std::runtime_error("Type not in variant");
        }
    }

    // getImpl递归
    template <typename T, typename TFirst, typename... TRest>
    T getImpl(const VariantStorage<TFirst, TRest...>* s) const {
        if (std::is_same<T, TFirst>::value) {
            return s->first;
        } else {
            return getImpl<T, TRest...>(&s->rest);
        }
    }

    // getImpl终止条件
    template <typename T, typename TLast>
    T getImpl(const VariantStorage<TLast>* s) const {
        if (std::is_same<T, TLast>::value) {
            return s->first;
        } else {
            throw std::runtime_error("Type not in variant");
        }
    }
};

#endif // SIMPLE_VARIANT_H
