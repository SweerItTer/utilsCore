#include <iostream>

#include "getCaptureNode.h"
#include "safeQueue.h"

int main(int argc, char const *argv[])
{
    // 模式1：仅捕获 rkispp_* 设备
    auto cameras = MediaHelper::GetNodesFromSysfs(false);
    std::cout << "===== RKISPP Devices =====" << std::endl;
    for (const auto& camera_pair : cameras) {
        std::cout << camera_pair.first << ": " << std::endl;
        for(const auto& node : camera_pair.second.nodes){
            std::cout << node.name << std::endl;
        }
    }

    // // 模式2：捕获所有设备（包括 USB 摄像头等）
    // auto all_nodes = MediaHelper::GetNodesFromSysfs(true);
    // std::cout << "\n===== All Devices =====" << std::endl;
    // for (const auto& node : all_nodes) {
    //     std::cout << node.name << " @ " << node.video_node << std::endl;
    // }

    return 0;
}
