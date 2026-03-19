/*
 * @FilePath: /src/utils/net/plugin.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-03-14
 * @LastEditors: Codex
 * @Description: Registrar plumbing for utils::net plugin handlers
 */

#include "net/plugin.h"

namespace utils {
namespace net {

HttpHandlerRegistrar::HttpHandlerRegistrar(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

bool HttpHandlerRegistrar::registerHandler(std::string handlerName,
                                           Handler handler,
                                           std::string* error) {
    if (!impl_) {
        if (error) *error = "Registrar is not initialized";
        return false;
    }
    if (handlerName.empty()) {
        if (error) *error = "Handler name must not be empty";
        return false;
    }
    if (!handler) {
        if (error) *error = "Handler callback is invalid";
        return false;
    }
    const auto inserted = impl_->handlers.emplace(std::move(handlerName), std::move(handler));
    if (!inserted.second) {
        if (error) *error = "Duplicate handler name in plugin instance '" + impl_->instanceName + "'";
        return false;
    }
    return true;
}

const std::string& HttpHandlerRegistrar::instanceName() const {
    static const std::string empty;
    return impl_ ? impl_->instanceName : empty;
}

} // namespace net
} // namespace utils
