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

            // 设备通道ID
            if (name.find("rkispp_m_bypass") != std::string::npos) {
                info.chnID = 0;  // 主通道
            } else if (name.find("rkispp_scale") != std::string::npos) {
                // 提取 "scaleX" 中的 X 并转换为数字
                size_t pos = name.find("scale");
                if (pos != std::string::npos) {
                    // 找到最后一位的下标
                    std::string num_str = name.substr(pos + 5); // "scale" 长度是 5
                    try {
                        int scale_num = std::stoi(num_str);
                        info.chnID = scale_num + 1;  // scale0 → chnID=1，scale1 → chnID=2
                    } catch (...) {
                        info.chnID = -1; // 解析失败
                    }
                } else {
                    info.chnID = -1;
                }
            } else {
                info.chnID = -1; // 非目标设备
            }

            result.push_back(std::move(info));
        }

        closedir(dir);
        return result;
    }

} // namespace MediaHelper