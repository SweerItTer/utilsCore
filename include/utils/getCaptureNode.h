#ifndef GET_CAPTURE_NODE_H
#define GET_CAPTURE_NODE_H

#include <string>
#include <vector>
#include <map>

namespace MediaHelper {
    
    /**
     * 节点信息结构体
     */
    struct NodeInfo {
        std::string name;        // 设备名称，如 "rkispp_m_bypass"
        std::string video_node;  // 视频节点路径，如 "/dev/video0"
        int chnID;              // 通道ID，主通道为0，scale通道为scale_num+1
    };
    
    /**
     * 相机组结构体
     */
    struct CameraGroup {
        std::vector<NodeInfo> nodes;  // 该相机的所有节点
        bool is_connected;           // 连接状态
    };
    
    /**
     * 解析rkispp设备的通道ID
     * @param device_name: 设备名称，如"rkispp_m_bypass", "rkispp_scale0"
     * @return: 通道ID，失败返回-1
     * 
     * @example:
     * parse_rkispp_channel("rkispp_m_bypass") -> 0
     * parse_rkispp_channel("rkispp_scale2")   -> 3
     */
    int parse_rkispp_channel(const std::string& device_name);
    
    /**
     * 检查设备连接状态（通过尝试打开scale2节点）
     * @param nodes: 节点列表
     * @return: 连接状态
     * 
     * @example:
     * check_camera_connection(camera_nodes) -> true/false
     */
    bool check_camera_connection(const std::vector<NodeInfo>& nodes);
    
    /**
     * 处理单个视频设备节点
     * @param base_path: 基础路径 "/sys/class/video4linux/"
     * @param device_name: 设备名称 "video0"
     * @param include_all: 是否包含所有设备
     * @param node_info: 输出参数，节点信息
     * @return: 处理是否成功
     * 
     * @example:
     * NodeInfo node_info;
     * if (process_single_device(base_path, "video0", false, node_info)) {
     * }
     */
    bool process_single_device(const std::string& base_path, 
                              const std::string& device_name,
                              bool include_all,
                              NodeInfo& node_info);
    
    /**
     * 主函数：从sysfs获取捕获节点信息
     * @param include_all: 是否包含所有设备类型
     * @return: 相机组映射表
     * @example:
     * auto cameras = GetNodesFromSysfs(false);
     * for (const auto& camera_pair : cameras) {
     *     std::cout << camera_pair.first << ": " 
     *               << camera_pair.second.nodes.size() << " nodes" << std::endl;
     * }
     */
    std::map<std::string, CameraGroup> GetNodesFromSysfs(bool include_all = false);
    
} // namespace MediaHelper

#endif // GET_CAPTURE_NODE_H