/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-30 21:50:28
 * @FilePath: /include/utils/v4l2param/paramControl.h
 */
#ifndef PARAM_CONTROL_H
#define PARAM_CONTROL_H

#include <string>
#include <vector>
#include <map>
#include <linux/videodev2.h>

struct V4L2ControlInfo {
    __u32 id = 0;
    std::string name = "";
    int32_t min = 0;
    int32_t max = 0;
    int32_t step = 0;
    // int32_t def = 0;
    // 选项类型 (值调整/状态开关)
    uint32_t type = 0;
    // uint32_t flags = 0;
    int32_t current = 0;

    inline bool operator==(const V4L2ControlInfo& other) const {
        return this->id == other.id &&
               this->name == other.name &&
               this->min == other.min &&
               this->max == other.max &&
            //    this->step == other.step &&
            //    this->def == other.def &&
            //    this->type == other.type &&
            //    this->flags == other.flags &&
               this->current == other.current;
    }
    inline bool operator!=(const V4L2ControlInfo& other) const {
        return !(*this == other);
    }
};

class ParamControl {
public:
    using ControlInfos = std::vector<V4L2ControlInfo>; 
    // 打开设备
    explicit ParamControl(const std::string& devicePath);

    // 使用已有 fd
    explicit ParamControl(int externalFd);

    ~ParamControl();

    // 设置/获取通用控制参数
    bool setControl(__u32 id, int32_t value);
    bool getControl(__u32 id, int32_t& value) const;

    // 查询单个控制参数
    bool queryControl(__u32 id);

    // 查找所有可调参数
    ControlInfos queryAllControls() const;

    // 字段对比
    static ControlInfos diffParamInfo(const ControlInfos& oldInfo,
        const ControlInfos& newInfo);
    // 控制类型判断
    static bool isSwitchControl(const V4L2ControlInfo& info);
    static bool isValueControl(const V4L2ControlInfo& info);
private:
    int fd_ = -1;
    bool ownsFd_ = false;  // 表示是否需要在析构时关闭
};

#endif // PARAM_CONTROL_H
