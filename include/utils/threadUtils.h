/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-30 19:38:50
 * @FilePath: /EdgeVision/include/utils/threadUtils.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
class ThreadUtils {
public:
    // 尝试绑定指定线程到指定 CPU 核心, 最多重试 retries 次  
    static bool safeBindThread(std::thread& thread, int cpu_core, int retries = 3) {
        for (int i = 0; i < retries; ++i) {
            if (thread.joinable()) {
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);
                CPU_SET(cpu_core, &cpuset);
                
                int result = pthread_setaffinity_np(thread.native_handle(), 
                                                    sizeof(cpu_set_t), &cpuset);
                if (result == 0) {
                    std::cout << "Successfully bound thread to core " << cpu_core << "\n";
                    return true;
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        std::cout << "Failed to bind thread to core " << cpu_core << "\n";
        return false;
    }
    // 直接在当前线程绑定CPU核心
    static bool bindCurrentThreadToCore(int cpu_core) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_core, &cpuset);
        int result = pthread_setaffinity_np(pthread_self(), 
                                sizeof(cpu_set_t), &cpuset);
        if (result == 0) {
            std::cout << "Successfully bound current thread to core " << cpu_core << "\n";
            return true;
        } 
        return false;
    }
};