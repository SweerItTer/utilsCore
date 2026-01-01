/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-12-13 05:28:26
 * @FilePath: /EdgeVision/src/pipeline/yoloProcessor.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include <deque>

#include "yoloProcessor.h"
#include "rknnPool.h"
#include "yolov5s.h"

#include "threadPauser.h"

class YoloProcessor::Impl {
public:
    Impl(const std::string& modelPath, 
         const std::string& classesTxtPath, 
         const size_t poolSize);
    ~Impl();

    void start();
    void stop();
    void pause();
    void resume();
    void setThresh(float BOX_THRESH_=-1.0f, float thNMS_THRESHresh_=-1.0f);
    int submit(DmaBufferPtr rgb, std::shared_ptr<void> holder);
    void setOnResult(ResultCB cb) { resultCallback_ = cb; }
    
private:
    void mainloop();
    
private:
    std::shared_ptr<
        rknnPool<Yolov5s, DmaBufferPtr, object_detect_result_list>
    > yoloPool_;

    ResultCB resultCallback_{nullptr};

    std::deque<std::shared_ptr<void>> holders;
    std::thread loop;
    std::atomic_bool running{false};
    std::atomic_bool ready{true};
    ThreadPauser pauser_;  // 替换 pause_ 标志
};

YoloProcessor::Impl::Impl(
    const std::string& modelPath, 
    const std::string& classesTxtPath, 
    const size_t poolSize) 
{
    auto poolSize_ = poolSize;
    if (poolSize <= 0) poolSize_ = 5;
    
    yoloPool_ = std::make_shared<
        rknnPool<Yolov5s, DmaBufferPtr, object_detect_result_list>
    >(modelPath, classesTxtPath, poolSize_);
    yoloPool_->init();
}

YoloProcessor::Impl::~Impl() {
    stop();
    yoloPool_->clearFutures();
}

void YoloProcessor::Impl::start() {
    if (running.exchange(true)) {
        return;  // 已经在运行
    }
    loop = std::thread(&YoloProcessor::Impl::mainloop, this);
}

void YoloProcessor::Impl::stop() {
    if (!running.exchange(false)) {
        return;  // 已经停止
    }
        
    // 确保线程能退出（如果暂停了, 先恢复）
    if (pauser_.is_paused()) {
        pauser_.resume();
    }
    
    if (loop.joinable()) {
        loop.join();
    }
    
    pauser_.close();
}

void YoloProcessor::Impl::pause() {
    pauser_.pause();
}

void YoloProcessor::Impl::resume() {
    pauser_.resume();
}

void YoloProcessor::Impl::setThresh(float BOX_THRESH_, float thNMS_THRESHresh_) {
    if (yoloPool_) yoloPool_->setThresh(BOX_THRESH_, thNMS_THRESHresh_);
}

int YoloProcessor::Impl::submit(DmaBufferPtr rgb, std::shared_ptr<void> holder)  {
    if (!ready) return -2;
    if (!rgb) return -1;
    if (!yoloPool_) return -1;
    holders.push_back(holder);
    return yoloPool_->put(rgb);
    ready.store(false);
}

void YoloProcessor::Impl::mainloop() {
    while (running) {
        // 检查暂停状态(是则堵塞完全让出CPU)
        pauser_.wait_if_paused();
        if (!running) break;
        if (!yoloPool_ || holders.size() < 1) continue;

        // 获取输出数据
        object_detect_result_list or_list;
        // 短超时获取
        yoloPool_->get(or_list, 500);
        // 回调返回结果(包括无结果)
        if (resultCallback_) resultCallback_(std::move(or_list));
        // 释放外部数据引用
        holders.pop_front();
        ready.store(true);
    }
}

// ----------------------- 对外接口 ---------------------------
YoloProcessor::YoloProcessor(const std::string& modelPath, 
    const std::string& classesTxtPath, 
    const size_t poolSize) : impl_(new Impl(modelPath, classesTxtPath, poolSize)){}
YoloProcessor::~YoloProcessor() {}
void YoloProcessor::submit(DmaBufferPtr rgb, std::shared_ptr<void> holder) {
    impl_->submit(std::move(rgb), std::move(holder));
}
void YoloProcessor::setOnResult(ResultCB cb) { impl_->setOnResult(cb); }
void YoloProcessor::setThresh(float BOX_THRESH_, float thNMS_THRESHresh_) {
    impl_->setThresh(BOX_THRESH_, thNMS_THRESHresh_);
}
void YoloProcessor::start() { impl_->start(); }
void YoloProcessor::stop() { impl_->stop(); }
void YoloProcessor::pause() { impl_->pause(); }
void YoloProcessor::resume() { impl_->resume(); }