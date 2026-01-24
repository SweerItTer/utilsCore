/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-31 19:37:23
 * @FilePath: /include/utils/v4l2param/paramLogger.h
 */
#ifndef PARAM_LOGGER_H
#define PARAM_LOGGER_H

#include <iostream>
#include <vector>
#include <string>

class ParamLogger {
public:
    static void setEnabled(bool enable);
    static void logChanges(const std::string& name);

private:
    static bool enabled_;
};

#endif // PARAM_LOGGER_H
