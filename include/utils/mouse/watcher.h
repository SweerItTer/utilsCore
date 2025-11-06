/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-23 16:22:53
 * @FilePath: /EdgeVision/include/utils/mouse/watcher.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
#include <thread>
#include <utility>
#include <sys/ioctl.h>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <linux/input.h>

#include "fdWrapper.h"      // fd RAII处理类
#include "udevMonitor.h"    // udev 监听类

class MouseWatcher {
    int clamp_int(int v, int lo, int hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }
private:
    std::mutex fdMtx, handlersMutex;
    
    FdWrapper mouseFd;
    std::atomic_bool running{false};
    std::atomic_bool paused{false};
    std::atomic<int> screenWidth, screenHeight;

    struct MouseEvent {
        int32_t x;
        int32_t y;
        std::unordered_map<uint16_t, uint8_t> buttons; // key code -> 0/1/2
        uint64_t sequence; // 用于检测数据新鲜度
    };
    
    // 对齐到缓存行, 避免伪共享
    std::vector<MouseEvent> m_events; // 双缓冲
    
    // 序列号, 用于确定哪个是最新数据
    std::atomic<uint64_t> m_sequence{0};

    // 回调管理
    using Callback = std::function<void(uint16_t, uint8_t)>;    // type, value
    struct Handler {
        std::function<bool(const uint16_t)> pred;
        Callback cb;
    };
    std::vector<Handler> handlers_;

public:
    explicit MouseWatcher() : screenWidth(0), screenHeight(0) {
        std::pair<FdWrapper, std::string> mouseInfo = std::move(getMouseInfo());
        std::lock_guard<std::mutex> fdLock(fdMtx);
        mouseFd = std::move(mouseInfo.first);
        std::string devicePath = mouseInfo.second;
        if (mouseFd.get() < 0) {
            fprintf(stderr, "[MouseWatcher] Not find any mouse device.\n");
        }
        
        // 注册设备变动回调
        UdevMonitor::registerHandler("input", {"change", "add", "remove"}, [this](){
            // 检查移除的设备是否是当前鼠标设备
            this->pause();
            std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // 等待系统稳定
            auto newMouseInfo = this->getMouseInfo();
            std::lock_guard<std::mutex> fdLock(fdMtx);
            // 更新 mouseFd
            this->mouseFd = std::move(newMouseInfo.first);
            if (this->mouseFd.get() < 0) {
                fprintf(stderr, "[MouseWatcher] Mouse device disconnected.\n");
            } else {
                fprintf(stdout, "[MouseWatcher] Mouse device changed to %s\n", newMouseInfo.second.c_str());
                this->start();
            }
        });
        // 初始化事件数据
        m_events.resize(2);
        
        fprintf(stdout, "[MouseWatcher] Mouse device: %s\n", devicePath.c_str());
    }
    
    void setScreenSize(int width, int height) {
        screenWidth.store(width);
        screenHeight.store(height);
    }

    ~MouseWatcher() {
        stop();
    }
    
    // 启动监听线程
    void start() {
        if (!running.load()) running.store(true);
        else if (paused.load()){
            paused.store(false);
            return;
        } else {    // running.load() == true || paused.load() == false
            return; // 已经在运行
        }
        std::thread(&MouseWatcher::Watch, this).detach();
    }
    
    // 停止监听
    void stop() {
        paused.store(false);
        running.store(false);
    }
    // 暂停监听
    void pause() {
        paused.store(true);
    }
    // 获取最新鼠标位置
    bool getPosition(int& x, int& y) {
        return getLatestPosition(x, y);
    }
    // 获取最新按键状态
    bool getKeyState(uint16_t key, uint8_t& value) {
        return getLatestKeyEvent(key, value);
    }

    // 注册事件处理回调
    void registerHandler(const std::vector<uint16_t>& eventCodes,  Callback cb) {
        // 原理参考 include/utils/udevMonitor.h
        std::unordered_set<uint16_t> actionSet(eventCodes.begin(), eventCodes.end());
        fprintf(stdout, "[MouseWatcher] registed actions: \t");
        for(auto& act : actionSet){
            fprintf(stdout, "%u, ", act);
        }
        fprintf(stdout, "\n");
        auto pred = [actionSet](const uint16_t evCode) -> bool {
            return (actionSet.find(evCode) != actionSet.end());
        };

        // 存入 handler
        std::lock_guard<std::mutex> lk(handlersMutex);
        handlers_.emplace_back(std::move(Handler{pred, cb}));
    }
protected:
    // 启动监听
    void Watch() {
        struct input_event ev;
        int32_t mouse_x = 0;
        int32_t mouse_y = 0;
        
        while (running.load()) {
            if (paused.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
            
            // ------- 读取事件 -------
            ssize_t bytes = 0;
            {
            std::lock_guard<std::mutex> fdLock(fdMtx);
            if (mouseFd.get() < 0) {
                // 无效的文件描述符, 稍作等待
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            bytes = read(mouseFd.get(), &ev, sizeof(ev));
            }
            // 读取失败或无数据, 稍作等待让出CPU
            if (0 > bytes) { std::this_thread::yield(); continue; }

            // ------- 回调事件优先调用 -------
            if (!handlers_.empty()) { // 无注册回调
                std::vector<Handler> handlers{};
                {
                std::lock_guard<std::mutex> lk(handlersMutex);
                handlers = handlers_; // 复制一份避免锁住太久
                }
                for (auto& handler : handlers) { // 这里不保证一定可以在主线程前join, 依赖使用者的逻辑
                    if (handler.pred(ev.code)) 
                        std::thread([code=ev.code, val=ev.value, cb=handler.cb]() { cb(code, val); }).detach();
                }
            }
            
            // -------  主动获取 -------
            const char* keyName = nullptr;
            switch (ev.code) {
                case REL_X:         mouse_x += ev.value; keyName = "X"; break;
                case REL_Y:         mouse_y += ev.value; keyName = "Y"; break;
                case REL_WHEEL:     keyName = "wheel vertical"; break;
                case REL_HWHEEL:    keyName = "wheel horizontal"; break;
                case BTN_LEFT:      keyName = "Left Button"; break;
                case BTN_RIGHT:     keyName = "Right Button"; break;
                case BTN_MIDDLE:    keyName = "Middle Button"; break;
                case BTN_SIDE:      keyName = "Back Side Button"; break;    // 侧后键
                case BTN_EXTRA:     keyName = "Forward Side Button"; break; // 侧前键
                default: break;
            }
            if (!keyName) continue; // 非关心事件
            if (screenWidth == 0 || screenHeight == 0) {
                fprintf(stderr, "[MouseWatcher] Warning: Screen size not set, mouse position clamping may be invalid.\n");
            }
            // 更新鼠标事件
            updateMouseEvent(mouse_x, mouse_y, ev.code, ev.value);
        }
    }
private:
    // 获取可用鼠标设备fd及路径
    std::pair<FdWrapper, std::string> getMouseInfo(){
        // 遍历 /dev/input 目录下的设备文件
        const char* inputDir = "/dev/input/";
        int fd = -1;
        std::string devicePath;
        
        for (int i = 0; i < 32; ++i) {
            // 打开设备
            std::string path = std::string(inputDir) + "event" + std::to_string(i);
            fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;
            
            // 检查设备busid是否为鼠标常用连接类型
            struct input_id id;
            if (ioctl(fd, EVIOCGID, &id) < 0) {
                close(fd);
                continue;
            }
            if (id.bustype != BUS_USB && id.bustype != BUS_BLUETOOTH) {
                close(fd);
                continue;
            }
            // 检查是否支持相对坐标(REL)
            unsigned long evbit = 0;
            if (ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), &evbit) < 0) {
                close(fd);
                continue;
            }
            unsigned long relbit = 0;
            // 检查是否支持相对坐标事件(EV_REL)
            if (!(evbit & (1 << EV_REL))) {
                close(fd);
                continue;
            }
            // 进一步检查是否支持X/Y轴相对移动
            if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relbit)), &relbit) < 0) {
                close(fd);
                continue;
            }
            if (relbit & (1 << REL_X) && relbit & (1 << REL_Y)) {
                devicePath = path;
                break;
            } 
            close(fd);
        }
        // fprintf(stdout, "Final Fd: %d\n", fd);
        return std::make_pair(std::move(FdWrapper(fd)), devicePath);
    }

    // 写入最新鼠标事件
    void updateMouseEvent(int& x, int& y, uint16_t key, uint8_t value) {
        uint64_t seq = m_sequence.load(std::memory_order_relaxed);
        int index = seq % 2;
        
        // 写入新数据到非活动缓冲区
        int other_index = 1 - index;
        x = clamp_int(x, 0, screenWidth - 1);
        y = clamp_int(y, 0, screenHeight - 1);
        m_events[other_index].x = x;
        m_events[other_index].y = y;
        if (key > 0) { // 0X000 是x,y坐标更新
            m_events[other_index].buttons[key] = value;
        }
        
        // 内存屏障确保数据写入完成后再更新序列号
        m_events[other_index].sequence = seq + 1;
        std::atomic_thread_fence(std::memory_order_release);
        
        // 切换活动缓冲区
        m_sequence.store(seq + 1, std::memory_order_release);
    }
    
    // 读取最新鼠标位置
    bool getLatestPosition(int& x, int& y) {
        uint64_t current_seq = m_sequence.load(std::memory_order_acquire);
        if (current_seq == 0) return false; // 尚无数据
        
        int index = current_seq % 2;
        
        // 确保读取完整的数据
        std::atomic_thread_fence(std::memory_order_acquire);
        x = m_events[index].x;
        y = m_events[index].y;
        return true;
    }
    // 读取最新按钮事件
    bool getLatestKeyEvent(uint16_t key, uint8_t& value) {
        uint64_t current_seq = m_sequence.load(std::memory_order_acquire);
        if (current_seq == 0) return false; // 尚无数据
        
        int index = current_seq % 2;
        
        // 确保读取完整的数据
        std::atomic_thread_fence(std::memory_order_acquire);
        auto it = m_events[index].buttons.find(key);
        if (it != m_events[index].buttons.end()) {
            value = it->second;
            return true;
        }
        return false;
    }
};