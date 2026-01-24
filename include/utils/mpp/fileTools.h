/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-01-12 19:37:10
 * @FilePath: /include/utils/mpp/fileTools.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 * @Description: 提供文件和目录相关的工具函数
 */

#include <sys/stat.h>

/// @brief 检查指定路径的目录是否存在
/// @param path 目录路径
/// @return 如果目录存在返回 true，否则返回 false
inline bool dirExists(const std::string& path) {
    struct stat st;
    if (0 != stat(path.c_str(), &st)) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}