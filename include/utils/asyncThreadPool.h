/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-05 19:23:07
 * @FilePath: /utilsCore/include/utils/asyncThreadPool.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef ASYNC_THREAD_POOL_H
#define ASYNC_THREAD_POOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include "concurrentqueue.h"
#include "internal/callbackTraits.h"
#include "internal/staticCallback.h"

// 管理线程检查间隔
static constexpr std::chrono::milliseconds MANAGERINTERVAL = std::chrono::milliseconds(5000);

/**
 * @brief 异步线程池类
 * 
 * 线程池管理一组工作线程, 可动态伸缩.
 * 支持任务优先级, 用户可选择阻塞或非阻塞入队.
 */
class asyncThreadPool {
public:
    // 线程池内部任务队列改成静态回调槽.
    // 目标是保留原有 enqueue/try_enqueue 用法,同时让热点任务避免落到 std::function<void()>.
    using TaskCallback = utils::internal::StaticCallback<void()>;

    /**
     * @brief 构造线程池
     * 
     * @param minThreads 最小线程数
     * @param maxThreads 最大线程数
     * @param maxQueueSize 队列最大长度
     */
    asyncThreadPool(std::size_t minThreads=-1, std::size_t maxThreads=-1, std::size_t maxQueueSize = 64);

    /**
     * @brief 析构函数, 停止线程池
     */
    ~asyncThreadPool();

    /**
     * @brief 阻塞入队任务(如果队列满, 会等待)
     * 
     * @tparam F 可调用对象类型
     * @tparam Args 参数类型
     * @param f 可调用对象
     * @param args 可调用对象参数
     * @return std::future<R> 返回任务执行结果的 future
     */
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using callableType = typename std::decay<F>::type;
        static_assert(
            utils::internal::is_invocable<callableType&, Args...>::value,
            "ThreadPool task must be invocable with the supplied argument types");
        using resultType = typename std::result_of<F(Args...)>::type;
        // 仍然使用 packaged_task 保留 future 语义,但真正入队的是静态绑定后的 TaskCallback.
        using taskType = std::packaged_task<resultType()>;
        auto taskPtr = std::make_shared<taskType>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<resultType> res = taskPtr->get_future();
        std::unique_lock<std::mutex> lock(queueMtx_);

        // 阻塞直到队列有空位
        queueNotFullCv_.wait(lock, [this]{ return tasks_.size_approx() < maxQueueSize_ || !running_; });

        if(!running_) return std::future<resultType>();

        tasks_.enqueue(TaskCallback::template bindShared<taskType, &taskType::operator()>(taskPtr));
        workerCv_.notify_one();

        return res;
    }

    template<class R, class Owner>
    auto enqueue(Owner* owner, R (Owner::*method)())
        -> std::future<R>
    {
        static_assert(std::is_same<typename std::remove_reference<Owner>::type, Owner>::value,
                      "Owner type must not be a reference");
        using taskType = std::packaged_task<R()>;
        auto taskPtr = std::make_shared<taskType>([owner, method]() {
            return (owner->*method)();
        });

        std::future<R> res = taskPtr->get_future();
        std::unique_lock<std::mutex> lock(queueMtx_);
        queueNotFullCv_.wait(lock, [this]{ return tasks_.size_approx() < maxQueueSize_ || !running_; });

        if(!running_) return std::future<R>();

        tasks_.enqueue(TaskCallback::template bindShared<taskType, &taskType::operator()>(taskPtr));
        workerCv_.notify_one();

        return res;
    }

    template<class R, class Owner>
    auto enqueue(const Owner* owner, R (Owner::*method)() const)
        -> std::future<R>
    {
        static_assert(std::is_same<typename std::remove_reference<Owner>::type, Owner>::value,
                      "Owner type must not be a reference");
        using taskType = std::packaged_task<R()>;
        auto taskPtr = std::make_shared<taskType>([owner, method]() {
            return (owner->*method)();
        });

        std::future<R> res = taskPtr->get_future();
        std::unique_lock<std::mutex> lock(queueMtx_);
        queueNotFullCv_.wait(lock, [this]{ return tasks_.size_approx() < maxQueueSize_ || !running_; });

        if(!running_) return std::future<R>();

        tasks_.enqueue(TaskCallback::template bindShared<taskType, &taskType::operator()>(taskPtr));
        workerCv_.notify_one();

        return res;
    }

    /**
     * @brief 非阻塞入队任务(如果队列满, 会直接返回空 future)
     * 
     * @tparam F 可调用对象类型
     * @tparam Args 参数类型
     * @param f 可调用对象
     * @param args 可调用对象参数
     * @return std::future<R> 返回任务执行结果的 future, 如果入队失败, 则 future invalid
     */
    template<class F, class... Args>
    auto try_enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using callableType = typename std::decay<F>::type;
        static_assert(
            utils::internal::is_invocable<callableType&, Args...>::value,
            "ThreadPool task must be invocable with the supplied argument types");
        using resultType = typename std::result_of<F(Args...)>::type;
        using taskType = std::packaged_task<resultType()>;
        auto taskPtr = std::make_shared<taskType>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<resultType> res = taskPtr->get_future();
        std::lock_guard<std::mutex> lock(queueMtx_);

        if(tasks_.size_approx() >= maxQueueSize_ || !running_)
            return std::future<resultType>(); // 队列满直接返回空 future

        tasks_.enqueue(TaskCallback::template bindShared<taskType, &taskType::operator()>(taskPtr));
        workerCv_.notify_one();

        return res;
    }

    template<class R, class Owner>
    auto try_enqueue(Owner* owner, R (Owner::*method)())
        -> std::future<R>
    {
        static_assert(std::is_same<typename std::remove_reference<Owner>::type, Owner>::value,
                      "Owner type must not be a reference");
        using taskType = std::packaged_task<R()>;
        auto taskPtr = std::make_shared<taskType>([owner, method]() {
            return (owner->*method)();
        });

        std::future<R> res = taskPtr->get_future();
        std::lock_guard<std::mutex> lock(queueMtx_);

        if(tasks_.size_approx() >= maxQueueSize_ || !running_)
            return std::future<R>();

        tasks_.enqueue(TaskCallback::template bindShared<taskType, &taskType::operator()>(taskPtr));
        workerCv_.notify_one();

        return res;
    }

    template<class R, class Owner>
    auto try_enqueue(const Owner* owner, R (Owner::*method)() const)
        -> std::future<R>
    {
        static_assert(std::is_same<typename std::remove_reference<Owner>::type, Owner>::value,
                      "Owner type must not be a reference");
        using taskType = std::packaged_task<R()>;
        auto taskPtr = std::make_shared<taskType>([owner, method]() {
            return (owner->*method)();
        });

        std::future<R> res = taskPtr->get_future();
        std::lock_guard<std::mutex> lock(queueMtx_);

        if(tasks_.size_approx() >= maxQueueSize_ || !running_)
            return std::future<R>();

        tasks_.enqueue(TaskCallback::template bindShared<taskType, &taskType::operator()>(taskPtr));
        workerCv_.notify_one();

        return res;
    }

    /**
     * @brief 阻塞投递任务(不需要 future 返回值的热点路径)
     * @return true: 成功投递; false: 线程池已停止
     */
    template<class F, class... Args>
    bool post(F&& f, Args&&... args)
    {
        using callableType = typename std::decay<F>::type;
        static_assert(
            utils::internal::is_invocable<callableType&, Args...>::value,
            "ThreadPool task must be invocable with the supplied argument types");

        auto bound = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        auto taskWrapper = [bound = std::move(bound)]() mutable {
            (void)bound();
        };
        using taskType = typename std::decay<decltype(taskWrapper)>::type;
        auto taskPtr = std::make_shared<taskType>(std::move(taskWrapper));

        std::unique_lock<std::mutex> lock(queueMtx_);
        queueNotFullCv_.wait(lock, [this]{ return tasks_.size_approx() < maxQueueSize_ || !running_; });
        if(!running_) return false;

        tasks_.enqueue(TaskCallback::template bindShared<taskType, &taskType::operator()>(taskPtr));
        workerCv_.notify_one();
        return true;
    }

    template<class Owner>
    bool post(Owner* owner, void (Owner::*method)())
    {
        auto taskWrapper = [owner, method]() mutable {
            (owner->*method)();
        };
        using taskType = typename std::decay<decltype(taskWrapper)>::type;
        auto taskPtr = std::make_shared<taskType>(std::move(taskWrapper));

        std::unique_lock<std::mutex> lock(queueMtx_);
        queueNotFullCv_.wait(lock, [this]{ return tasks_.size_approx() < maxQueueSize_ || !running_; });
        if(!running_) return false;

        tasks_.enqueue(TaskCallback::template bindShared<taskType, &taskType::operator()>(taskPtr));
        workerCv_.notify_one();
        return true;
    }

    template<class Owner>
    bool post(const Owner* owner, void (Owner::*method)() const)
    {
        auto taskWrapper = [owner, method]() mutable {
            (owner->*method)();
        };
        using taskType = typename std::decay<decltype(taskWrapper)>::type;
        auto taskPtr = std::make_shared<taskType>(std::move(taskWrapper));

        std::unique_lock<std::mutex> lock(queueMtx_);
        queueNotFullCv_.wait(lock, [this]{ return tasks_.size_approx() < maxQueueSize_ || !running_; });
        if(!running_) return false;

        tasks_.enqueue(TaskCallback::template bindShared<taskType, &taskType::operator()>(taskPtr));
        workerCv_.notify_one();
        return true;
    }

    /**
     * @brief 非阻塞投递任务(不需要 future 返回值的热点路径)
     * @return true: 成功投递; false: 队列满或线程池停止
     */
    template<class F, class... Args>
    bool try_post(F&& f, Args&&... args)
    {
        using callableType = typename std::decay<F>::type;
        static_assert(
            utils::internal::is_invocable<callableType&, Args...>::value,
            "ThreadPool task must be invocable with the supplied argument types");

        auto bound = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        auto taskWrapper = [bound = std::move(bound)]() mutable {
            (void)bound();
        };
        using taskType = typename std::decay<decltype(taskWrapper)>::type;
        auto taskPtr = std::make_shared<taskType>(std::move(taskWrapper));

        std::lock_guard<std::mutex> lock(queueMtx_);
        if(tasks_.size_approx() >= maxQueueSize_ || !running_) return false;

        tasks_.enqueue(TaskCallback::template bindShared<taskType, &taskType::operator()>(taskPtr));
        workerCv_.notify_one();
        return true;
    }

    template<class Owner>
    bool try_post(Owner* owner, void (Owner::*method)())
    {
        auto taskWrapper = [owner, method]() mutable {
            (owner->*method)();
        };
        using taskType = typename std::decay<decltype(taskWrapper)>::type;
        auto taskPtr = std::make_shared<taskType>(std::move(taskWrapper));

        std::lock_guard<std::mutex> lock(queueMtx_);
        if(tasks_.size_approx() >= maxQueueSize_ || !running_) return false;

        tasks_.enqueue(TaskCallback::template bindShared<taskType, &taskType::operator()>(taskPtr));
        workerCv_.notify_one();
        return true;
    }

    template<class Owner>
    bool try_post(const Owner* owner, void (Owner::*method)() const)
    {
        auto taskWrapper = [owner, method]() mutable {
            (owner->*method)();
        };
        using taskType = typename std::decay<decltype(taskWrapper)>::type;
        auto taskPtr = std::make_shared<taskType>(std::move(taskWrapper));

        std::lock_guard<std::mutex> lock(queueMtx_);
        if(tasks_.size_approx() >= maxQueueSize_ || !running_) return false;

        tasks_.enqueue(TaskCallback::template bindShared<taskType, &taskType::operator()>(taskPtr));
        workerCv_.notify_one();
        return true;
    }

    /**
     * @brief 手动停止线程池
     */
    void stop();

    /**
     * @brief 获取当前工作线程数量(不含已标记停止的线程)
     */
    size_t aliveThreadCount() const;

private:
    // ----------------- 内部类型 -----------------
    struct WorkerWrapper; // 工作线程封装, 记录活跃时间与停止标志

    // ----------------- 内部函数 -----------------
    void worker(std::weak_ptr<WorkerWrapper> wrapper);      // 工作线程函数
    void managerThreadFunc();                                 // 管理线程函数
    void adjustThreads();                                     // 动态扩缩容逻辑

    // ----------------- 数据成员 -----------------
    std::atomic<bool> running_;
    std::atomic<size_t> activeTasks_{0};                      // 当前活跃任务数

    mutable std::mutex queueMtx_;
    std::condition_variable workerCv_;
    std::condition_variable queueNotFullCv_;

    moodycamel::ConcurrentQueue<TaskCallback> tasks_;                          // 任务队列
    std::size_t maxQueueSize_;

    std::thread managerThread_;                               // 管理线程
    std::vector<std::shared_ptr<WorkerWrapper>> workers_;

    size_t minThreads_;
    size_t maxThreads_;
};

#endif
