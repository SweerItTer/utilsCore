#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstring>
#include <iostream>
#include <unordered_map>

#include "v4l2param/paramControl.h"

ParamControl::ParamControl(const std::string& devicePath) {
    fd_ = open(devicePath.c_str(), O_RDWR);
    if (-1 == fd_) {
        perror("[ParamControl] Failed to open device");
    } else {
        ownsFd_ = true;
    }
}

ParamControl::ParamControl(int externalFd) : fd_(externalFd), ownsFd_(false) {
    // 不拥有 fd,因此析构时不关闭
}

ParamControl::~ParamControl() {
    if (ownsFd_ && fd_ >= 0) {
        close(fd_);
    }
}

bool ParamControl::setControl(__u32 id, int32_t value) {
    struct v4l2_control ctrl = {};
    ctrl.id = id;
    ctrl.value = value;
    if (-1 == ioctl(fd_, VIDIOC_S_CTRL, &ctrl)) {
        perror("[ParamControl] VIDIOC_S_CTRL failed");
        return false;
    }
    return true;
}

bool ParamControl::getControl(__u32 id, int32_t& value) const {
    struct v4l2_control ctrl = {};
    ctrl.id = id;
    if (-1 == ioctl(fd_, VIDIOC_G_CTRL, &ctrl)) {
        perror("[ParamControl] VIDIOC_G_CTRL failed");
        return false;
    }
    value = ctrl.value;
    return true;
}

bool ParamControl::queryControl(__u32 id) {
    struct v4l2_queryctrl query = {};
    query.id = id;
    if (-1 == ioctl(fd_, VIDIOC_QUERYCTRL, &query)) {
        perror("[ParamControl] VIDIOC_QUERYCTRL failed");
        return false;
    }

    std::cout << "[Query] Control: " << query.name
              << " [" << query.minimum << ", "
              << query.maximum << "] Default: "
              << query.default_value << std::endl;
    return true;
}

ParamControl::ControlInfos ParamControl::queryAllControls() const {
    /* 写给未来的自己或其他维护者

    像我这种初学者常常对 "优化代码" 有一种执念,尤其是刚了解一些底层知识后
    会倾向于纠结栈上变量,循环内结构体创建等细节,幻想实现 "内核级优化"
    实际上
    1. 本函数(queryAllControls)大概率只会在初始化阶段调用一次,不存在高频调用场景
    2. 将 struct v4l2_queryctrl query 放在循环内/外,对性能可以说是几乎没有影响(微秒级)
    3. 现代编译器(-O2/-O3)会自动优化栈变量复用,消除冗余初始化
    
    要是想优化,应该关注的是系统级问题
        - 减少 ioctl 调用次数
        - 异步/线程化处理任务
        - 避免无效状态同步
        - 缓存结构体结果,复用堆资源
        (之类的)

    所以,不要在这种细节上产生压力
    (当然了,如果是出于代码的清晰程度,你想拿出来就拿出来,无所谓)
    */
    ControlInfos controls;
    for (__u32 id = V4L2_CID_BASE; id < V4L2_CID_LASTP1; ++id) {
        struct v4l2_queryctrl query = {};
        query.id = id;

        if (-1 == ioctl(fd_, VIDIOC_QUERYCTRL, &query)) {
            continue;
        }

        if (query.flags & V4L2_CTRL_FLAG_DISABLED) {
            continue;
        }

        V4L2ControlInfo info;
        info.id    = query.id;
        // 转为字符串
        info.name  = reinterpret_cast<const char*>(query.name);
        // 标定可调项的范围
        info.min   = query.minimum;
        info.max   = query.maximum;
        // 可选项
        info.step  = query.step;
        // info.def   = query.default_value;
        info.type  = query.type;
        // info.flags = query.flags;
        // 获取当前值
        getControl(query.id, info.current);

        controls.push_back(info);
    }

    // // 查询特殊扩展控制
    // for (__u32 id = V4L2_CID_PRIVATE_BASE; id < V4L2_CID_PRIVATE_BASE + 0x1000; ++id) {
    //     struct v4l2_queryctrl query = {};
    //     query.id = id;

    //     if (-1 == ioctl(fd_, VIDIOC_QUERYCTRL, &query)) {
    //         continue;
    //     }

    //     if (query.flags & V4L2_CTRL_FLAG_DISABLED) {
    //         continue;
    //     }

    //     V4L2ControlInfo info;
    //     info.id    = query.id;
    //     info.name  = reinterpret_cast<const char*>(query.name);
    //     info.min   = query.minimum;
    //     info.max   = query.maximum;
    //     info.step  = query.step;
    //     info.def   = query.default_value;
    //     info.type  = query.type;
    //     info.flags = query.flags;

    //     controls.push_back(info);
    // }

    return controls;
}

ParamControl::ControlInfos ParamControl::diffParamInfo(const ControlInfos& oldInfo,
    const ControlInfos& newInfo)
{
    ControlInfos result;
    
    // 采用hash表存储,避开 std::find_if的O(n^2)时间复杂度
    std::unordered_map<__u32, int32_t> oldValueMap;
    for (const auto& oldEntry : oldInfo){
        oldValueMap[oldEntry.id] = oldEntry.current;
    }
    
    // 遍历所有可调参数
    for (const auto& newEntry : newInfo) {
        auto it = oldValueMap.find(newEntry.id); //O(1) 查找时间复杂度
        if (it != oldValueMap.end() && it->second == newEntry.current) {
            continue; // 配置无变化
        }

        V4L2ControlInfo diffInfo;
        diffInfo.id = newEntry.id;
        diffInfo.name = newEntry.name;

        // 数值合法性判断
        if (newEntry.current < newEntry.min) diffInfo.current = newEntry.min;
        else if (newEntry.current > newEntry.max) diffInfo.current = newEntry.max;
        else diffInfo.current = newEntry.current;
        
        result.push_back(diffInfo);
    }

    return result;
}

bool ParamControl::isSwitchControl(const V4L2ControlInfo& info) {
    return V4L2_CTRL_TYPE_BOOLEAN == info.type;
}

bool ParamControl::isValueControl(const V4L2ControlInfo& info) {
    return V4L2_CTRL_TYPE_INTEGER == info.type ||
           V4L2_CTRL_TYPE_MENU == info.type ||
           V4L2_CTRL_TYPE_INTEGER64 == info.type;
}
