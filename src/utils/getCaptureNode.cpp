#include "getCaptureNode.h"
#include <fstream>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <cstring>
#include <stdlib.h>
#include <fcntl.h>
#include <iostream>
#include <sys/ioctl.h>
#include <cstdio>
#include <memory>
#include <array>
#include <algorithm> // 添加头文件用于排序
#include <sstream>   // 添加头文件用于字符串流操作

#include <linux/videodev2.h>

namespace MediaHelper {
    
    int parse_rkispp_channel(const std::string& device_name) {
        if (device_name.find("rkispp_m_bypass") != std::string::npos) {
            return 0;  // 主通道
        }
        
        size_t pos = device_name.find("rkispp_scale");
        if (pos != std::string::npos) {
            std::string num_str = device_name.substr(pos + 12);
            try {
                int scale_num = std::stoi(num_str);
                return scale_num + 1;
            } catch (const std::exception& e) {
                std::cerr << "Error parsing scale number from '" << device_name 
                         << "': " << e.what() << std::endl;
                return -1;
            }
        }
        
        return -1;
    }
    
    // 提取设备号（video后面的数字）
    int extract_video_number(const std::string& device_node) {
        // 设备节点格式：/dev/videoXX
        size_t pos = device_node.find_last_not_of("0123456789");
        if (pos != std::string::npos && pos < device_node.size() - 1) {
            try {
                return std::stoi(device_node.substr(pos + 1));
            } catch (...) {
                return -1;
            }
        }
        return -1;
    }
    
    bool process_single_device(const std::string& base_path, 
                              const std::string& device_name,
                              bool include_all,
                              NodeInfo& node_info) {
        // 基本验证
        if (strncmp(device_name.c_str(), "video", 5) != 0) {
            return false;
        }
        
        const std::string full_path = base_path + device_name;
        
        // 验证目录存在
        struct stat stat_buf;
        if (stat(full_path.c_str(), &stat_buf) != 0 || !S_ISDIR(stat_buf.st_mode)) {
            return false;
        }
        
        // 读取设备名称
        const std::string name_path = full_path + "/name";
        std::ifstream name_ifs(name_path);
        if (!name_ifs.is_open()) {
            std::cerr << "Failed to open name file: " << name_path << std::endl;
            return false;
        }
        
        std::string device_display_name;
        if (!std::getline(name_ifs, device_display_name)) {
            std::cerr << "Failed to read device name from: " << name_path << std::endl;
            return false;
        }
        name_ifs.close();
        
        // 检查设备类型
        const bool is_rkispp = (device_display_name.find("rkispp_") != std::string::npos);
        if (!is_rkispp && !include_all) {
            return false;
        }
        
        // 解析通道ID
        int channel_id = parse_rkispp_channel(device_display_name);
        if (channel_id == -1 && !include_all) {
            return false;
        }
        
        // 构建节点信息
        node_info.name = device_display_name;
        node_info.video_node = "/dev/" + device_name;
        node_info.chnID = channel_id;
        
        // 获取物理设备路径
        // const std::string device_link = full_path + "/device";
        // char device_path[PATH_MAX];
        // ssize_t len = readlink(device_link.c_str(), device_path, sizeof(device_path)-1);
        // if (len <= 0) {
        //     std::cerr << "Failed to read device link: " << device_link << std::endl;
        //     return false;
        // }
        
        // device_path[len] = '\0';
        // char abs_path[PATH_MAX];
        // if (realpath(device_path, abs_path) == nullptr) {
        //     std::cerr << "Failed to resolve path: " << device_path << std::endl;
        //     return false;
        // }
        // physical_path = abs_path;
        
        return true;
    }
    
    // 按设备号分组（连续4个节点为一组）
    std::map<int, std::vector<NodeInfo>> group_by_device_number(std::vector<NodeInfo>& all_nodes) {
        std::map<int, std::vector<NodeInfo>> groups;
        
        // 按设备号排序
        std::sort(all_nodes.begin(), all_nodes.end(), [](const NodeInfo& a, const NodeInfo& b) {
            int num_a = extract_video_number(a.video_node);
            int num_b = extract_video_number(b.video_node);
            return num_a < num_b;
        });
        
        // 分组逻辑：连续4个节点为一组
        for (auto& node : all_nodes) {
            int video_num = extract_video_number(node.video_node);
            if (video_num == -1) continue;
            
            // 查找合适的组（设备号连续）
            bool found_group = false;
            for (auto& group_pair : groups) {
                int base_num = group_pair.first;
                auto& group = group_pair.second;
                if (video_num >= base_num && video_num < base_num + 4) {
                    group.push_back(node);
                    found_group = true;
                    break;
                }
            }
            
            // 如果没有找到合适的组，创建新组
            if (!found_group) {
                groups[video_num].push_back(node);
            }
        }
        
        return groups;
    }
    
    std::map<std::string, CameraGroup> GetNodesFromSysfs(bool include_all) {
        const std::string base_path = "/sys/class/video4linux/";
        std::map<std::string, CameraGroup> result;
        std::vector<NodeInfo> all_nodes; // 存储所有节点
        
        // 1. 扫描目录并收集所有节点
        DIR* dir = opendir(base_path.c_str());
        if (!dir) {
            std::cerr << "Failed to open directory: " << base_path << std::endl;
            return result;
        }
        
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            NodeInfo node_info;
            
            if (process_single_device(base_path, entry->d_name, include_all, node_info)) {
                all_nodes.push_back(node_info);
                
                // 调试输出
                std::cout << "Found device: " << node_info.name 
                         << " -> " << node_info.video_node 
                         << " (channel " << node_info.chnID << ")" << std::endl;
            }
        }
        closedir(dir);
        
        // 2. 按设备号分组（连续4个节点为一组）
        auto device_groups = group_by_device_number(all_nodes);
        
        // 3. 创建最终结果
        int cam_index = 0;
        for (auto& group_pair : device_groups) {
            int base_num = group_pair.first;
            auto& nodes = group_pair.second;
                    
            // 每个物理摄像头应该有4个节点
            if (nodes.size() < 4) {
                std::cerr << "Incomplete camera group at base " << base_num 
                         << " with only " << nodes.size() << " nodes" << std::endl;
                continue;
            }
            
            CameraGroup group;
            group.nodes = nodes;
            
            std::stringstream cam_key;
            cam_key << "cam" << cam_index++;
            result[cam_key.str()] = group;
            
            // 调试输出
            std::cout << "Camera group " << cam_key.str() << ": " 
                     << group.nodes.size() << " nodes "<< std::endl;
        }
        
        std::cout << "Total found " << result.size() << " camera groups" << std::endl;
        
        return result;
    }
    
} // namespace MediaHelper