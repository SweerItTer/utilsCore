/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-30 19:38:50
 * @FilePath: /include/utils/threadUtils.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once

class ThreadUtils {
public:
    // 尝试绑定指定线程到指定 CPU 核心, 最多重试 retries 次  
    static bool safeBindThread(std::thread& thread, int cpuCore, int retries = 3) {
        for (int i = 0; i < retries; ++i) {
            if (thread.joinable()) {
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(cpuCore, &cpuset);
                
                int result = pthread_setaffinity_np(thread.native_handle(), 
                                                    sizeof(cpu_set_t), &cpuset);
                if (result == 0) {
                    std::cout << "Successfully bound thread to core " << cpuCore << "\n";
                    return true;
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        std::cout << "Failed to bind thread to core " << cpuCore << "\n";
        return false;
    }
    // 绑定当前线程到CPU核心
    static bool bindCurrentThreadToCore(int cpuCore) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpuCore, &cpuset);
        int result = pthread_setaffinity_np(pthread_self(), 
                                sizeof(cpu_set_t), &cpuset);
        if (result == 0) {
            std::cout << "Successfully bound current thread to core " << cpuCore << "\n";
            return true;
        } 
        return false;
    }
    // 绑定线程到指定CPU核心
    static void bindThreadToCore(std::thread& thread, int core) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core, &cpuset);
        pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t), &cpuset);
    }
    // FIFO (数值越大, 优先级越高 1 ~ 99 )
    static void setRealtimeThread(pthread_t stl_thread_handle, int priority) {
        sched_param sch{};
        sch.sched_priority = priority;
        if (0 != pthread_setschedparam(stl_thread_handle, SCHED_FIFO, &sch)) {
            perror("pthread_setschedparam");
        }
    }
};