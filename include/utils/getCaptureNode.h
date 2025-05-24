#ifndef GET_CAPTURE_NODE_H
#define GET_CAPTURE_NODE_H

#include <string>
#include <vector>

namespace MediaHelper {

    struct NodeInfo {
        std::string name;           ///< 节点名称（如 rkispp_scale0）
        std::string video_node;     ///< 视频设备节点路径（如 /dev/video31）
        std::string device_path;    ///< 系统设备路径（如 /sys/devices/...）
    };

    /**
     * @brief 获取所有 rkispp 视频节点信息(如 rkispp_m_bypass、rkispp_scaleX 等)
     * @param include_all 是否输出全部设备
     * @return std::vector<NodeInfo> 匹配的rkispp设备信息集合
     * 
     * @note 依赖 /sys/class/video4linux 下的节点与 /name 文件匹配。
     * @example
     *     auto nodes = MediaHelper::GetNodesFromSysfs(false);
     *     for (const auto& node : nodes) {
     *         std::cout << node.name << ": " << node.video_node << " => " << node.device_path << std::endl;
     *     }
     */
    std::vector<NodeInfo> GetNodesFromSysfs(bool include_all);

} // namespace MediaHelper

#endif // GET_CAPTURE_NODE_H
