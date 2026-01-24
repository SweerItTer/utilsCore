/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-15 00:32:44
 * @FilePath: /include/utils/udevMonitor.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef UDEV_MONITOR_H
#define UDEV_MONITOR_H

#include <libudev.h>
#include <sys/epoll.h>
#include <thread>
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <unordered_set>
#include <cstddef>

/**
 * @brief 监听 Linux udev 事件的单例类
 *        - 支持注册多个 (subsystem, actions, callback)
 *        - 内部使用 epoll + udev_monitor 监听事件
 *        - 后台线程自动运行, 用户无需手动 start/stop
 *        - 回调在后台线程执行, 用户需避免耗时操作阻塞
 */
class UdevMonitor {
public:
    // 回调类型
    using Callback = std::function<void()>;

    /**
     * @brief 注册一个回调处理函数
     * @param subsystem  设备子系统 (如 "drm", "video4linux")
     * @param actions    关心的动作 (如 {"add","remove","change"})
     * @param cb         匹配时调用的回调函数
     *
     * 注册时会自动构造 predicate, 并在第一次注册时自动启动后台线程
     */
    static void registerHandler(const std::string& subsystem,
                                const std::vector<std::string>& actions,
                                Callback cb);

    /**
     * @brief 主动停止监听 (通常不需要手动调用)
     *        程序退出时单例析构会自动 stop
     */
    static void stop();

    // 获取单例实例
    static UdevMonitor& instance();

private:
    UdevMonitor();
    ~UdevMonitor();

    // 禁止拷贝
    UdevMonitor(const UdevMonitor&) = delete;
    UdevMonitor& operator=(const UdevMonitor&) = delete;

    // 内部结构: 保存 predicate 和 callback
    struct Handler {
        std::function<bool(const std::string& subs, const std::string& act)> pred;
        Callback cb;
    };

    // 启动后台线程 (仅在首次注册时调用)
    void startIfNeededUnlocked();

    // 后台线程执行的事件循环
    void run();

    // 资源
    struct udev* udev_{nullptr};
    struct udev_monitor* monitor_{nullptr};
    int epollFd_{-1};
    int monitorFd_{-1};

    std::thread worker_;
    std::atomic<bool> running_{false};

    std::mutex handlersMutex_;
    std::vector<Handler> handlers_;
};

#endif // UDEV_MONITOR_H
