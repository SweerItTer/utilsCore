/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-31 19:30:53
 * @FilePath: /EdgeVision/include/utils/v4l2param/paramProcessor.h
 */
#ifndef PARAM_PROCESSOR_H
#define PARAM_PROCESSOR_H

#include "v4l2param/paramLogger.h"
#include "v4l2param/paramControl.h"

#include <thread>
#include <mutex>
#include <atomic>
#include <functional>

class ParamProcessor {
public:
    using Callback = std::function<void(const std::string& name, int value)>;
    using ErrorCallback = std::function<void(const std::string& msg)>;

    explicit ParamProcessor(const std::string& devicePath); // 自动 open
    explicit ParamProcessor(int externalFd);                // 使用外部 fd

    ParamProcessor(ParamProcessor&& other) noexcept;
    ParamProcessor& operator=(ParamProcessor&& other) noexcept;

    ParamProcessor(const ParamProcessor&) = delete;
    ParamProcessor& operator=(const ParamProcessor&) = delete;

    ~ParamProcessor();

    // 调试信息相关
    void enableDebugLog(bool enable);
    void setSuccessCallback(Callback cb);
    void setErrorCallback(ErrorCallback cb);

    void start();
    void stop();

    void setTargetControls(const ParamControl::ControlInfos& controlList);

    // 获取当前所有参数
    ParamControl::ControlInfos& getCurrentControls();
    ParamControl& getCurrentController() { return param_; }
private:
    void threadLoop();
    bool applyChange(const V4L2ControlInfo& change);

    ParamControl param_;
    
    std::thread thread_;
    std::mutex mutex_;
    std::atomic<bool> running_ {false};

    ParamControl::ControlInfos currentControls_;
    ParamControl::ControlInfos pendingControls_;
    // 可选log对象
    Callback successCallback_;
    ErrorCallback errorCallback_;
};

#endif // PARAM_PROCESSOR_H
