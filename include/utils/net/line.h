/*
 * @FilePath: /include/utils/net/line.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-02-22
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 * @Description: Line protocol types (COMMAND [PARAMS]\\n) for utils::net
 */
#pragma once

#include <string>

namespace utils {
namespace net {

struct LineRequest {
    std::string command;
    std::string params;
    std::string raw;
};

} // namespace net
} // namespace utils

