#ifndef RKNNPOOL_H
#define RKNNPOOL_H

#include "asyncThreadPool.h"
#include <vector>
#include <iostream>
#include <mutex>
#include <queue>
#include <memory>

// rknnModel模型类, inputType模型输入类型, outputType模型输出类型
template <typename rknnModel, typename inputType, typename outputType>
class rknnPool
{
private:
    const int threadNum;        // 线程池大小
    std::string modelPath;      // 模型路径 
    std::string COCOPath;       // 类别文件路径

    std::atomic<float> BOX_THRESH{0.25f};   // 默认BOX阈值
    std::atomic<float> thNMS_THRESHresh{0.45f};

    std::atomic<long long> id;  // 模型标志 id(用与在异步线程池内区分模型结果)
    std::mutex futMtx;          // future 锁

    using ModelList = std::vector<std::shared_ptr<rknnModel>>;
    using Futures   = std::queue<std::future<outputType>>;
    using ThreadPool= std::unique_ptr<asyncThreadPool>;
    
    Futures futs;        // 线程 future
    ThreadPool pool;     // 线程池
    ModelList models;    // 模型类
protected:
    int getModelId();

public:
    // 初始化类对象
    rknnPool(const std::string& modelPath, const std::string &COCOPath, int threadNum)
        : modelPath(modelPath), COCOPath(COCOPath), threadNum(threadNum), id(0) { }

    int init();
    // 模型推理/Model inference
    int put(inputType inputData);
    // 获取推理结果/Get the results of your inference
    int get(outputType &outputData, int timeout);
    // 清空所有 future
    void clearFutures(){
        auto temp = std::queue<std::future<outputType>>();
        futs.swap(temp); // 清空队列
    }
    void setThresh(float BOX_THRESH_=-1.0f, float thNMS_THRESHresh_=-1.0f) {
        if (BOX_THRESH_ != BOX_THRESH.load() && BOX_THRESH_ > 0.0f) BOX_THRESH.store(BOX_THRESH_);
        if (thNMS_THRESHresh_ != thNMS_THRESHresh.load() && thNMS_THRESHresh_ > 0.0f) thNMS_THRESHresh.store(thNMS_THRESHresh_);
    };
    ~rknnPool();
};

template <typename rknnModel, typename inputType, typename outputType>
int rknnPool<rknnModel, inputType, outputType>::init()
{
    try {
        pool.reset( new asyncThreadPool(threadNum, threadNum) ); // 实例化线程池
        // 入队模型类(包含模型上下文初始化等)
        for (int i = 0; i < threadNum; i++)
            models.push_back( std::make_shared<rknnModel>(modelPath, COCOPath) );
    } catch (const std::bad_alloc &e) {
        std::cout << "Out of memory: " << e.what() << std::endl;
        return -1;
    }
    
    auto& topContext = models[0]->getCurrentContext();
    for (int i = 0, ret = 0; i < threadNum; i++) {
        // 对每一个模型对象进行初始化(模型加载等)
        ret = models[i]->init(topContext, i != 0);
        if (ret != 0)
            return ret;
    }

    return 0;
}

template <typename rknnModel, typename inputType, typename outputType>
int rknnPool<rknnModel, inputType, outputType>::getModelId()
{
    int modelId = id.load() % threadNum;
    id.fetch_add(1);
    return modelId;
}

template <typename rknnModel, typename inputType, typename outputType>
int rknnPool<rknnModel, inputType, outputType>::put(inputType inputData)
{
    int currentId = getModelId();
    std::lock_guard<std::mutex> lock(futMtx);
    // 将模型处理函数作为工作内容交由线程池
    std::future<outputType> future = pool->try_enqueue([this, currentId, inputData = std::move(inputData)](){
        std::shared_ptr<rknnModel>& model = models[currentId];
        model->setThresh(BOX_THRESH.load(), thNMS_THRESHresh.load());
        return model->infer(inputData);
    });
    if (future.valid()){
        if (futs.size() >= 30) futs.pop();
        futs.emplace(std::move(future)); // 入队当前处理future(这里就保证了顺序)
        return 0;
    }
    return 1;
}


template <typename rknnModel, typename inputType, typename outputType>
int rknnPool<rknnModel, inputType, outputType>::get(outputType &outputData, int timeout_ms)
{
    outputData = outputType(); // 清空输出数据
    std::lock_guard<std::mutex> lock(futMtx);
    if(futs.empty() == true)
        return 1;
    std::future<outputType>& currentfut = futs.front();
    if ( timeout_ms == 0 ){
        while (currentfut.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready);
    } else {
        if (currentfut.wait_for(std::chrono::milliseconds(timeout_ms)) != std::future_status::ready){
            // futs.pop();
            return -1;
        }
    }
    outputData = currentfut.get();    // 取出最旧的 future,并通过.get获取数据内容
    futs.pop();
    return 0;
}

template <typename rknnModel, typename inputType, typename outputType>
rknnPool<rknnModel, inputType, outputType>::~rknnPool()
{
    pool->stop();
    clearFutures();
}
#endif
