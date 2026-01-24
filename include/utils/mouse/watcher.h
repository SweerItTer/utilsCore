/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-23 16:22:53
 * @FilePath: /include/utils/mouse/watcher.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// 鼠标事件类型枚举
enum class MouseEventType : uint16_t {
    AxisX = 0x00,           // X轴相对移动
    AxisY = 0x01,           // Y轴相对移动
    WheelVertical = 0x08,   // 垂直滚轮
    WheelHorizontal = 0x06, // 水平滚轮
    ButtonLeft = 0x110,     // 左键
    ButtonRight = 0x111,    // 右键
    ButtonMiddle = 0x112,   // 中键
    ButtonSide = 0x113,     // 侧后键
    ButtonExtra = 0x114,    // 侧前键
    Unknown = 0xFFFF        // 未知事件
};

class MouseWatcher {
public:
    // 回调函数类型定义
    using EventCallback = std::function<void(MouseEventType, uint8_t)>;  // 事件类型, 值
    using PositionCallback = std::function<void(int32_t, int32_t)>;      // x, y

    // 构造和析构
    explicit MouseWatcher();
    ~MouseWatcher();

    // 禁止拷贝和移动
    MouseWatcher(const MouseWatcher&) = delete;
    MouseWatcher& operator=(const MouseWatcher&) = delete;
    MouseWatcher(MouseWatcher&&) = delete;
    MouseWatcher& operator=(MouseWatcher&&) = delete;

    // 生命周期管理
    void start();
    void stop();
    void pause();
    void resume();

    // 分辨率设置
    void setScreenSize(int width, int height);
    void setTargetSize(int width, int height);

    // 主动获取坐标 - 原始屏幕坐标
    bool getRawPosition(int& x, int& y);
    
    // 主动获取坐标 - 映射到目标分辨率的坐标
    bool getMappedPosition(int& x, int& y);
    
    // 兼容旧接口
    bool getPosition(int& x, int& y);

    // 获取按键状态
    bool getKeyState(MouseEventType key, uint8_t& value);

    // 注册事件回调
    void registerHandler(const std::vector<MouseEventType>& eventTypes, EventCallback cb);
    
    // 注册位置回调
    void registerRawPositionCallback(PositionCallback cb);
    void registerMappedPositionCallback(PositionCallback cb);

private:
    // PImpl 惯用法, 隐藏实现细节
    class Impl;
    std::unique_ptr<Impl> impl;
};
