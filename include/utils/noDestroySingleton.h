#ifndef NO_DESTROY_SINGLETON_H
#define NO_DESTROY_SINGLETON_H

#include <utility>

namespace utils {

template <typename Factory>
auto& noDestroySingleton(Factory&& factory) {
    using Pointer = decltype(factory());
    static Pointer instance = factory();
    return *instance;
}

} // namespace utils

#endif // NO_DESTROY_SINGLETON_H
