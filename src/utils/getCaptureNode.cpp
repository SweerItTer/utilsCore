#include "getCaptureNode.h"

#include <fstream>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <cstring>

namespace MediaHelper {

    std::vector<NodeInfo> GetNodesFromSysfs(bool include_all) {
        const std::string base_path = "/sys/class/video4linux/";
        std::vector<NodeInfo> result;

        DIR* dir = opendir(base_path.c_str());
        if (!dir) return result;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            // 跳过 "." 和 ".."
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            // 验证是否为 videoX 目录
            const std::string full_path = base_path + entry->d_name;
            struct stat stat_buf;
            if (stat(full_path.c_str(), &stat_buf) != 0 || !S_ISDIR(stat_buf.st_mode) ||
                strncmp(entry->d_name, "video", 5) != 0) {
                continue;
            }

            // 读取设备名称
            const std::string name_path = full_path + "/name";
            std::ifstream name_ifs(name_path);
            if (!name_ifs) continue;

            std::string name;
            if (!std::getline(name_ifs, name)) continue;

            // 核心逻辑：始终捕获 rkispp_ 设备，其他设备由参数控制
            const bool is_rkispp = (name.find("rkispp_") != std::string::npos);
            if (!is_rkispp && !include_all) {
                continue; // 非目标设备且未开启全捕获模式，跳过
            }

            // 构建节点信息
            NodeInfo info;
            info.name = name;
            info.video_node = "/dev/" + std::string(entry->d_name);

            // 解析设备路径（同之前的安全逻辑）
            const std::string symlink_path = full_path + "/device";
            char resolved_path[PATH_MAX];
            ssize_t len = readlink(symlink_path.c_str(), resolved_path, sizeof(resolved_path)-1);
            if (len > 0) {
                resolved_path[len] = '\0';
                char* real_path = realpath(resolved_path, nullptr);
                if (real_path) {
                    info.device_path = real_path;
                    free(real_path);
                }
            }

            result.push_back(std::move(info));
        }

        closedir(dir);
        return result;
    }

} // namespace MediaHelper