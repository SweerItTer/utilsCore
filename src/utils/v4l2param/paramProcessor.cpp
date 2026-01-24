/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-31 19:38:35
 * @FilePath: /src/utils/v4l2param/paramProcessor.cpp
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

ParamControl::ControlInfos& ParamProcessor::getCurrentControls() {
    return currentControls_;
}

void ParamProcessor::threadLoop() {
    while (running_) {
        ParamControl::ControlInfos temp;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            // 检查是否有新配置
            if (pendingControls_.empty() && currentControls_.empty()) {
                // 首次运行, 尝试获取当前所有控制项
                pendingControls_ = param_.queryAllControls();
            }
            
            // 如果配置相同, 等待
            if (pendingControls_ == currentControls_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                continue;
            }
            
            // 锁内拷贝
            temp = pendingControls_;
        }
        // 找出所有修改过的参数
        auto changed = param_.diffParamInfo(currentControls_, temp);
        
        // 应用配置
        bool all_success = true;
        for (const auto& per : changed) {
            ParamLogger::logChanges(per.name);
            if (!applyChange(per)) {
                all_success = false;
            }
        }
        
        // 更新配置
        if (all_success) {
            std::lock_guard<std::mutex> lock(mutex_);
            currentControls_ = temp;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}

bool ParamProcessor::applyChange(const V4L2ControlInfo& change) {
    // 重试机制
    for (int i = 0; i < 3; ++i) {
        bool ok = param_.setControl(change.id, change.current);
        if (ok) {
            if (successCallback_) {
                successCallback_(change.name, change.current);
            }
            return true;
        }
        
        if (i < 2) {  // 前两次失败后等待重试
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    // 所有重试都失败
    if (errorCallback_) {
        auto msg = "Failed to set: [" + change.name + "] -> " + 
                    std::to_string(change.current) + " after 3 attempts";
        errorCallback_(msg);
    }
    return false;
}


// paramProcessor.cpp
ParamProcessor::ParamProcessor(ParamProcessor&& other) noexcept
    : param_(std::move(other.param_))
    , thread_(std::move(other.thread_))
    , running_(other.running_.load())
    , currentControls_(std::move(other.currentControls_))
    , pendingControls_(std::move(other.pendingControls_))
    , successCallback_(std::move(other.successCallback_))
    , errorCallback_(std::move(other.errorCallback_)) {
    // 确保原对象不再运行
    other.running_.store(false);
}

ParamProcessor& ParamProcessor::operator=(ParamProcessor&& other) noexcept {
    if (this != &other) {
        // 停止当前对象的线程
        stop();
        
        *this = std::move(other);
    }
    return *this;
}