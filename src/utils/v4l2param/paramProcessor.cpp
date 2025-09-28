/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-31 19:38:35
 * @FilePath: /EdgeVision/src/utils/v4l2param/paramProcessor.cpp
 */
#include "v4l2param/paramProcessor.h"
#include <chrono>

// 使用外部 fd
/* 这里的用法和下面的等价
ParamProcessor::ParamProcessor(int externalFd)
    : param_(ParamControl(externalFd))
*/
ParamProcessor::ParamProcessor(int externalFd)
    : param_(externalFd) 
{
    enableDebugLog(true);
}

// 自动打开设备路径
ParamProcessor::ParamProcessor(const std::string& devicePath)
    : param_(devicePath) 
{
    enableDebugLog(true);
}

ParamProcessor::~ParamProcessor() {
    stop();
}

void ParamProcessor::start() {
    running_ = true;
    thread_ = std::thread(&ParamProcessor::threadLoop, this);
}

void ParamProcessor::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ParamProcessor::setTargetControls(const ParamControl::ControlInfos& controlList) {
    std::lock_guard<std::mutex> lock(mutex_);
    // 外部更新配置
    pendingControls_ = controlList;
}

void ParamProcessor::setSuccessCallback(Callback cb) {
    successCallback_ = std::move(cb);
}

void ParamProcessor::setErrorCallback(ErrorCallback cb) {
    errorCallback_ = std::move(cb);
}

void ParamProcessor::enableDebugLog(bool enable) {
    ParamLogger::setEnabled(enable);
}

void ParamProcessor::threadLoop() {
    while (running_) {
        auto temp = pendingControls_;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // 若配置一致则不管
            if (temp == currentControls_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                continue;
            }
            
        }
        // 找出所有修改过的参数
        auto changed = param_.diffParamInfo(currentControls_, temp);
        // 应用配置
        for (const auto& per : changed){
            ParamLogger::logChanges(per.name);
            applyChange(per);
        }
        
        // 更新配置
        /***** 优化建议: 根据 applyChange 成功与否更新(但是感觉实际没什么用) *****/
        currentControls_ = temp;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}

void ParamProcessor::applyChange(const V4L2ControlInfo& change) {

    bool ok = param_.setControl(change.id, change.current);

    if (ok && successCallback_) {
        successCallback_(change.name, change.current);
    } else if (!ok && errorCallback_) {
        auto log = "Failed to set: [" + change.name + "] -> " + std::to_string(change.current) + "\n";
        errorCallback_(log);
    }
}
