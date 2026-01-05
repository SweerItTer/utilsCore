#pragma once

#include <atomic>
#include <memory>

/**
 * @brief Linux 平台特供的线程暂停控制器
 * 
 * 基于 eventfd 实现, 提供高性能的线程暂停/恢复机制
 * 特点: 
 * 1. 真正的内核级阻塞, 暂停时不消耗CPU
 * 2. 零锁设计, 无锁竞争
 * 3. 支持暂停状态检查
 * 4. 支持超时唤醒
 * 5. 线程安全, 可在多线程环境中使用
 * 
 * @note 仅支持 Linux 平台, 依赖 eventfd 系统调用
 * @note 如果需要跨平台支持, 请使用条件变量版本
 */
class ThreadPauser {
public:
    /**
     * @brief 构造函数
     * @throws std::system_error 如果 eventfd 创建失败
     */
    ThreadPauser();
    
    /**
     * @brief 析构函数, 自动关闭资源
     */
    ~ThreadPauser();
    
    // 禁用拷贝构造和拷贝赋值
    ThreadPauser(const ThreadPauser&) = delete;
    ThreadPauser& operator=(const ThreadPauser&) = delete;
    
    /**
     * @brief 移动构造函数
     */
    ThreadPauser(ThreadPauser&& other) noexcept;
    
    /**
     * @brief 移动赋值运算符
     */
    ThreadPauser& operator=(ThreadPauser&& other) noexcept;
    
    /**
     * @brief 等待, 如果已暂停则阻塞, 否则立即返回
     * @throws std::system_error 如果系统调用失败且未关闭
     */
    void wait_if_paused();
    
    /**
     * @brief 带超时的等待
     * @param timeout_ms 超时时间(毫秒)
     * @return true - 等待成功(恢复或未暂停), false - 超时
     */
    bool wait_if_paused_for(int timeout_ms);
    
    /**
     * @brief 暂停线程
     * 
     * 线程将在下一次调用 wait_if_paused() 时阻塞
     */
    void pause();
    
    /**
     * @brief 恢复线程
     * 
     * 唤醒所有等待的线程
     * @throws std::system_error 如果系统调用失败且未关闭
     */
    void resume();
    
    /**
     * @brief 切换暂停状态
     */
    void toggle();
    
    /**
     * @brief 检查是否处于暂停状态
     * @return true - 已暂停, false - 未暂停
     */
    bool is_paused() const;
    
    /**
     * @brief 关闭控制器, 唤醒所有等待的线程
     * 
     * 调用后对象不可再使用
     */
    void close();
    
    /**
     * @brief 检查控制器是否已关闭
     * @return true - 已关闭, false - 未关闭
     */
    bool is_closed() const;

private:
    // PImpl 惯用法, 隐藏实现细节
    class Impl;
    std::unique_ptr<Impl> impl_;
    
    // 内部辅助函数
    void ensure_not_closed() const;
};