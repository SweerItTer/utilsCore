/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-31 19:38:35
 * @FilePath: /EdgeVision/src/utils/v4l2param/paramLogger.cpp
 */
#include "v4l2param/paramLogger.h"

bool ParamLogger::enabled_ = false;

void ParamLogger::setEnabled(bool enable) {
    // 是否允许输出 log
    enabled_ = enable;
}

void ParamLogger::logChanges(const std::string& name) {
    if (!enabled_) return;
    // 输出变更的参数
    std::cout << "[ParamLogger] Changed: " << name << "\n";
}
