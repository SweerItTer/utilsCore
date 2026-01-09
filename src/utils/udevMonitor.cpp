#include <unistd.h>
#include <iostream>
#include <cstring>
#include <errno.h>
#include <unordered_map>

#include "udevMonitor.h"
#include "asyncThreadPool.h"

// 获取单例实例
UdevMonitor& UdevMonitor::instance() {
    // 和程序共存亡
    static UdevMonitor inst;
    return inst;
}

UdevMonitor::UdevMonitor() {
    // 懒加载: 仅在第一次 registerHandler 开启线程
}

UdevMonitor::~UdevMonitor() {
    stop();
}

/**
 * @brief 注册回调
 *        - 将 subsystem + actions 封装成 predicate
 *        - 存储到 handlers_ 中
 *        - 启动后台线程(如未启动)
 */
void UdevMonitor::registerHandler(const std::string& subsystem,
                                  const std::vector<std::string>& actions,
                                  Callback cb) {
    // 把 actions 存成 set, O(1) 查找
    std::unordered_set<std::string> actionSet(actions.begin(), actions.end());

    /* 构造 predicate, 捕获 subsystem + actionSet
     * 只有满足 predicate 才调用 cb
     */
    auto pred = [subsystem, actionSet](const std::string& subs, const std::string& act) -> bool {
        if (subs != subsystem) return false;
        std::cout << "subs:" << subs << std::endl;
        return (actionSet.find(act) != actionSet.end());
    };

    {
        // 存入 handler
        UdevMonitor &self = instance();
        std::lock_guard<std::mutex> lk(self.handlersMutex_);
        self.handlers_.push_back(Handler{pred, cb});
    }

    // 确保后台线程已经启动
    instance().startIfNeededUnlocked();
}

/**
 * @brief 主动停止 udev 监听, 清理资源
 */
void UdevMonitor::stop() {
    UdevMonitor &self = instance();

    bool expected = true;
    if (!self.running_.compare_exchange_strong(expected, false)) {
        return; // 已经停止, 直接返回
    }

    // 等待后台线程退出
    if (self.worker_.joinable()) {
        self.worker_.join();
    }

    // 关闭 epoll
    if (self.epollFd_ != -1) {
        close(self.epollFd_);
        self.epollFd_ = -1;
    }

    // 释放 monitor
    if (self.monitor_) {
        udev_monitor_unref(self.monitor_);
        self.monitor_ = nullptr;
    }

    // 释放 udev
    if (self.udev_) {
        udev_unref(self.udev_);
        self.udev_ = nullptr;
    }
}

/**
 * @brief 确保后台线程启动
 *        - 如果已经 running, 直接返回
 *        - 否则初始化 udev + epoll, 启动线程
 */
void UdevMonitor::startIfNeededUnlocked() {
    UdevMonitor &self = instance();

    // 如果已运行,直接返回
    if (self.running_) {
        return;
    }

    // 期待 running_ 是 false
    bool expected = false;
    /* running_ 的期待(expected)是 false, 尝试改为 true
     * 如果返回 true 表示成功将 expected(false)->strong(true)
     * 反之表示 running_ 已经是 true */
    bool ok = self.running_.compare_exchange_strong(expected, true);
    if (false == ok) {
        return; // 别的线程已经启动了
    }

    // 初始化 udev
    self.udev_ = udev_new();
    if (!self.udev_) {
        std::cerr << "UdevMonitor: failed to create udev context\n";
        self.running_ = false;
        return;
    }
    // 创建监视器, 设置事件源为udev(可选项还有 kernel)
    self.monitor_ = udev_monitor_new_from_netlink(self.udev_, "udev");
    if (!self.monitor_) {
        std::cerr << "UdevMonitor: failed to create udev monitor\n";
        udev_unref(self.udev_);
        self.udev_ = nullptr;
        self.running_ = false;
        return;
    }
    // 启动 monitor
    if (udev_monitor_enable_receiving(self.monitor_) < 0) {
        std::cerr << "UdevMonitor: enable_receiving failed\n";
        udev_monitor_unref(self.monitor_);
        self.monitor_ = nullptr;
        udev_unref(self.udev_);
        self.udev_ = nullptr;
        self.running_ = false;
        return;
    }
    // 导出 monitor fd
    self.monitorFd_ = udev_monitor_get_fd(self.monitor_);
    if (self.monitorFd_ < 0) {
        std::cerr << "UdevMonitor: invalid monitor fd\n";
        udev_monitor_unref(self.monitor_);
        self.monitor_ = nullptr;
        udev_unref(self.udev_);
        self.udev_ = nullptr;
        self.running_ = false;
        return;
    }

    // 创建 epoll fd
    self.epollFd_ = epoll_create1(0); // flag 定义了fd的行为, 0 表示不启用任何额外的标志
    if (self.epollFd_ < 0) {
        std::cerr << "UdevMonitor: epoll_create1 failed: " << strerror(errno) << "\n";
        udev_monitor_unref(self.monitor_);
        self.monitor_ = nullptr;
        udev_unref(self.udev_);
        self.udev_ = nullptr;
        self.running_ = false;
        return;
    }
    // 配置 epoll 监听 monitorFd_
    epoll_event ev{};
    ev.events = EPOLLIN;            // 设置监听事件类型
    ev.data.fd = self.monitorFd_;   // 设置监听的 fd

    // 绑定 fd 和 事件 并添加到epoll红黑树
    if (epoll_ctl(self.epollFd_, EPOLL_CTL_ADD, self.monitorFd_, &ev) < 0) {
        std::cerr << "UdevMonitor: epoll_ctl failed: " << strerror(errno) << "\n";
        close(self.epollFd_);
        self.epollFd_ = -1;
        udev_monitor_unref(self.monitor_);
        self.monitor_ = nullptr;
        udev_unref(self.udev_);
        self.udev_ = nullptr;
        self.running_ = false;
        return;
    }

    // 启动线程
    self.worker_ = std::thread(&UdevMonitor::run, &self);
}

/**
 * @brief 后台线程: epoll + udev 循环
 *        - 等待事件
 *        - 解析 subsystem/action/devpath
 *        - 调用匹配的回调
 */
void UdevMonitor::run() {
    constexpr int MAX_EVENTS = 8;
    epoll_event events[MAX_EVENTS];
    asyncThreadPool asyncPool(2);
    
    // 防抖: 记录最近触发的事件 <subsystem+devpath, 时间戳>
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastTriggerTime;
    std::mutex debounceMutex;
    constexpr int DEBOUNCE_MS = 500;  // 500ms 防抖时间
    
    while (running_) {
        // 轮询 monitor 是否有drm类型的可读事件
        // 并将使用events接收事件
        int n = epoll_wait(epollFd_, events, MAX_EVENTS, 1000); // 1000ms 超时
        if (n == 0) continue;
        if (n < 0) {
            if (errno == EINTR) continue;
            std::cerr << "UdevMonitor: epoll_wait error: " << strerror(errno) << "\n";
            break;
        }

        for (int i = 0; i < n; ++i) {
            // 只关系当前 udev monitor
            if (events[i].data.fd != monitorFd_) continue;
            if (!(events[i].events & EPOLLIN)) continue;

            // 获取产生事件的设备映射
            struct udev_device* dev = udev_monitor_receive_device(monitor_);
            if (!dev) continue;

            const char* action_c = udev_device_get_action(dev);         // 获取设备事件
            const char* devpath_c = udev_device_get_devpath(dev);       // 获取设备位置
            const char* subsystem_c = udev_device_get_subsystem(dev);   // 获取子系统设备

            if (action_c && devpath_c && subsystem_c) {
                std::string action(action_c);
                std::string devpath(devpath_c);
                std::string subs(subsystem_c);

                // 构造唯一键: subsystem + devpath + action
                std::string eventKey = subs + ":" + devpath + ":" + action;
                
                // 防抖检查
                bool shouldTrigger = false;
                {
                    std::lock_guard<std::mutex> lock(debounceMutex);
                    auto now = std::chrono::steady_clock::now();
                    auto it = lastTriggerTime.find(eventKey);
                    
                    if (it == lastTriggerTime.end()) {
                        // 首次触发
                        shouldTrigger = true;
                        lastTriggerTime[eventKey] = now;
                    } else {
                        // 检查时间间隔
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - it->second).count();
                        
                        if (elapsed >= DEBOUNCE_MS) {
                            shouldTrigger = true;
                            it->second = now;
                        } else {
                            std::cout << "[UdevMonitor] Debounce: ignored " << eventKey 
                                      << " (elapsed: " << elapsed << "ms)" << std::endl;
                        }
                    }
                    
                    // 清理过期的记录(超过 5 秒未触发)
                    for (auto iter = lastTriggerTime.begin(); iter != lastTriggerTime.end();) {
                        auto age = std::chrono::duration_cast<std::chrono::seconds>(
                            now - iter->second).count();
                        if (age > 5) {
                            iter = lastTriggerTime.erase(iter);
                        } else {
                            ++iter;
                        }
                    }
                }
                
                if (!shouldTrigger) {
                    udev_device_unref(dev);
                    continue;
                }
                
                // 拷贝 handler 列表, 避免持锁执行回调
                std::vector<Handler> snapshot;
                {
                    std::lock_guard<std::mutex> lk(handlersMutex_);
                    snapshot = handlers_;
                }

                // 调用匹配的 handler
                for (auto &h : snapshot) {
                    // 查询是否是监听的设备和对应事件
                    if (false == h.pred(subs, action)) continue;
                    try {
                        // 异步调用回调
                        // asyncPool.enqueue([](){h.cb();});
                        asyncPool.enqueue(h.cb);

                    } catch (const std::exception& e) {
                        std::cerr << "UdevMonitor: handler exception: " << e.what() << "\n";
                    } catch (...) {
                        std::cerr << "UdevMonitor: handler unknown exception\n";
                    }
                }
            }
            // 释放设备映射
            udev_device_unref(dev);
        }
    }
    
    // 线程退出时清理资源
    if (epollFd_ != -1) {
        close(epollFd_);
        epollFd_ = -1;
    }
    if (monitor_) {
        udev_monitor_unref(monitor_);
        monitor_ = nullptr;
    }
    if (udev_) {
        udev_unref(udev_);
        udev_ = nullptr;
    }
}
