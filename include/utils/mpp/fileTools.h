/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-01-12 19:37:10
 * @FilePath: /include/utils/mpp/fileTools.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 * @Description: 提供文件和目录相关的工具函数
 */
#pragma once
#include <sys/stat.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

/// @brief 检查指定路径的目录是否存在
/// @param path 目录路径
/// @return 如果目录存在返回 true, 否则返回 false
inline bool dirExists(const std::string& path) {
    struct stat st;
    if (0 != stat(path.c_str(), &st)) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

inline bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), value.begin());
}

inline std::string normalizeJoinPath(const std::string& base, const std::string& suffix) {
    if (suffix.empty()) {
        return base;
    }
    if (base.empty()) {
        return suffix;
    }
    if (suffix.front() == '/') {
        return base + suffix;
    }
    return base + "/" + suffix;
}

inline bool isExternalSdBlockDevice(const std::string& device) {
    return startsWith(device, "/dev/mmcblk") &&
           !startsWith(device, "/dev/mmcblk0");
}

inline int sdMountPriority(const std::string& mountPoint) {
    if (mountPoint == "/mnt/sdcard") return 100;
    if (startsWith(mountPoint, "/mnt/sdcard_")) return 90;
    if (mountPoint == "/media/sdcard0") return 80;
    if (mountPoint == "/media/sdcard1") return 70;
    if (startsWith(mountPoint, "/media/sdcard")) return 60;
    return 0;
}

inline std::string findMountedSdCardPath() {
    std::ifstream mounts("/proc/mounts");
    if (!mounts.is_open()) {
        return {};
    }

    std::string bestMount;
    int bestPriority = 0;
    std::string line;
    while (std::getline(mounts, line)) {
        std::istringstream iss(line);
        std::string device;
        std::string mountPoint;
        std::string fsType;
        if (!(iss >> device >> mountPoint >> fsType)) {
            continue;
        }
        if (!isExternalSdBlockDevice(device)) {
            continue;
        }
        const int priority = sdMountPriority(mountPoint);
        if (priority <= 0 || !dirExists(mountPoint)) {
            continue;
        }
        if (priority > bestPriority) {
            bestPriority = priority;
            bestMount = mountPoint;
        }
    }
    return bestMount;
}

inline bool hasMountedSdCard() {
    return !findMountedSdCardPath().empty();
}

inline std::string resolveSdCardSaveDir(const std::string& preferredPath) {
    const std::string mountedPath = findMountedSdCardPath();
    if (mountedPath.empty()) {
        return {};
    }

    if (preferredPath.empty()) {
        return mountedPath;
    }

    static const std::vector<std::string> aliases = {
        "/mnt/sdcard",
        "/media/sdcard0",
        "/media/sdcard1"
    };
    for (const auto& alias : aliases) {
        if (!startsWith(preferredPath, alias)) {
            continue;
        }
        std::string suffix = preferredPath.substr(alias.size());
        if (suffix == "/") {
            suffix.clear();
        }
        return normalizeJoinPath(mountedPath, suffix);
    }

    return preferredPath;
}
