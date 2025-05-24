#include <iostream>

#include "getCaptureNode.h"
#include "safeQueue.h"

int main(int argc, char const *argv[])
{
    // 模式1：仅捕获 rkispp_* 设备
    auto rkispp_nodes = MediaHelper::GetNodesFromSysfs(false);
    std::cout << "===== RKISPP Devices =====" << std::endl;
    for (const auto& node : rkispp_nodes) {
        std::cout << node.name << " @ " << node.video_node << std::endl;
    }

    // 模式2：捕获所有设备（包括 USB 摄像头等）
    auto all_nodes = MediaHelper::GetNodesFromSysfs(true);
    std::cout << "\n===== All Devices =====" << std::endl;
    for (const auto& node : all_nodes) {
        std::cout << node.name << " @ " << node.video_node << std::endl;
    }

    return 0;
}
