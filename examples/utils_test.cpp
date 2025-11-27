/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-25 13:09:10
 * @FilePath: /EdgeVision/examples/utils_test.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "fbshow.h"

#include <csignal>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <unistd.h>

#include "concurrentqueue.h"
#include "orderedQueue.h"
#include "asyncThreadPool.h"
#include "mouse/watcher.h"
#include "sys/cpuMonitor.h"
#include "sys/memoryMonitor.h"
#include "encoderTest.h"

using namespace DrmDev;

static std::atomic_bool running{true};
static void handleSignal(int signal) {
    if (signal == SIGINT) {
        std::cout << "Ctrl+C received, stopping..." << std::endl;
        running.store(false);
    }
}

struct TimedBuffer { 
    DmaBufferPtr buffer = nullptr;
    std::chrono::steady_clock::time_point enqueue_time;
};

// -------------------- MPMC 极限测试 --------------------
int mpmcTestMaxPerf() {
    const size_t maxQueueSize = 64; // 最大队列长度
    moodycamel::ConcurrentQueue<TimedBuffer> testQueue;
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    std::atomic<int> total_produced{0};
    std::atomic<int> total_consumed{0};

    const int producerCount = 10;
    const int consumerCount = 10;
    auto start = std::chrono::steady_clock::now();

    running = true;

    // 生产者
    for (int i = 0; i < producerCount; i++) {
        producers.emplace_back([&testQueue, &total_produced] {
            while (running) {
                if (testQueue.size_approx() > maxQueueSize) {
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                    continue;
                }
                TimedBuffer item;
                item.buffer = DmaBuffer::create(640, 640, DRM_FORMAT_RGB888, 0, 0);
                item.enqueue_time = std::chrono::steady_clock::now();
                testQueue.enqueue(std::move(item));
                total_produced++;
            }
        });
    }

    // 消费者
    for (int i = 0; i < consumerCount; i++) {
        consumers.emplace_back([&testQueue, &total_consumed] {
            TimedBuffer item;
            while (running) {
                if (testQueue.try_dequeue(item)) {
                    // 极限性能下的简单处理：获取 buffer 大小
                    volatile auto sz = item.buffer->width() * item.buffer->height();
                    (void)sz;
                    total_consumed++;
                }
            }
        });
    }

    // 监控线程可选，避免影响性能
    std::thread monitor([&testQueue] {
        while (running) {
            std::cout << "[MPMC] Approx queue size: " << testQueue.size_approx() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    // 测试持续 10 秒
    std::this_thread::sleep_for(std::chrono::seconds(10));
    running = false;

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();
    monitor.join();

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "\n===== MPMC MaxPerf Report =====\n";
    std::cout << "Elapsed: " << elapsed_ms << " ms\n";
    std::cout << "Total produced: " << total_produced << "\n";
    std::cout << "Total consumed: " << total_consumed << "\n";
    std::cout << "Throughput: " << (total_consumed * 1000.0 / elapsed_ms) << " items/sec\n";
    std::cout << "Final queue size: " << testQueue.size_approx() << std::endl;

    return 0;
}

// -------------------- SPSC 极限测试 --------------------
int spscTestMaxPerf() {
    SafeQueue<std::unique_ptr<TimedBuffer>> testQueue(1024); // 适当增大容量避免阻塞
    std::thread producer;
    std::thread consumer;
    std::atomic<int> total_produced{0};
    std::atomic<int> total_consumed{0};

    running = true;
    auto start = std::chrono::steady_clock::now();

    // 生产者
    producer = std::thread([&testQueue, &total_produced] {
        while (running) {
            auto item = std::make_unique<TimedBuffer>();
            item->buffer = DmaBuffer::create(640, 640, DRM_FORMAT_RGB888, 0, 0);
            item->enqueue_time = std::chrono::steady_clock::now();
            testQueue.enqueue(std::move(item));
            total_produced++;
        }
    });

    // 消费者
    consumer = std::thread([&testQueue, &total_consumed] {
        std::unique_ptr<TimedBuffer> item;
        while (running) {
            if (testQueue.try_dequeue(item)) {
                volatile auto sz = item->buffer->width() * item->buffer->height();
                (void)sz;
                total_consumed++;
            }
        }
    });

    std::thread monitor([&testQueue] {
        while (running) {
            std::cout << "[SPSC] Queue size: " << testQueue.size() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(10));
    running = false;

    if (producer.joinable()) producer.join();
    if (consumer.joinable()) consumer.join();
    monitor.join();

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "\n===== SPSC MaxPerf Report =====\n";
    std::cout << "Elapsed: " << elapsed_ms << " ms\n";
    std::cout << "Total produced: " << total_produced << "\n";
    std::cout << "Total consumed: " << total_consumed << "\n";
    std::cout << "Throughput: " << (total_consumed * 1000.0 / elapsed_ms) << " items/sec\n";
    std::cout << "Final queue size: " << testQueue.size() << std::endl;

    return 0;
}

int orderedQueueTest() {
    constexpr int poolSize = 4;
    OrderedQueue<FramePtr> orderedQueue(9999);
    constexpr size_t maxQueueSize = 20;        // ConcurrentQueue 临时缓冲大小
    moodycamel::ConcurrentQueue<FramePtr> testQueue;

    std::atomic<int> total_produced{0};         // 总捕获帧数
    std::atomic<int> total_consumed{0};         // 总RGA处理帧
    std::atomic<int> total_dequeued{0};         // 总排序出队数量
    std::atomic<bool> order_violation{false};   // 有序判别标志

    // ----------------- 生产者线程 -----------------
    std::thread v4l2CapturerThread([&](){
        std::atomic<uint64_t> frame_id{0};      // 单调递增 frame_id
        while (running.load()) {
            // 控制队列最大长度
            if (testQueue.size_approx() >= maxQueueSize) {
                std::this_thread::sleep_for(std::chrono::microseconds(5));
                continue;
            }
            // 模拟构造一帧
            auto sptr = std::make_shared<SharedBufferState>(
                DmaBuffer::create(640, 640, DRM_FORMAT_RGB888, 0, 0));
            FramePtr frame(new Frame(sptr));
            frame->meta.frame_id = frame_id.fetch_add(1, std::memory_order_relaxed);

            testQueue.enqueue(frame);
            total_produced.fetch_add(1, std::memory_order_relaxed);
            // 模拟捕获耗时
            std::this_thread::sleep_for(std::chrono::microseconds(20));
        }
    });

    // ----------------- 消费者线程 -----------------
    asyncThreadPool RGAThreadPool(poolSize);
    for (int i = 0; i < poolSize; ++i) {
        RGAThreadPool.enqueue([&]{
            while (running.load() || testQueue.size_approx() > 0) {
                FramePtr frame;
                if (!testQueue.try_dequeue(frame)) {
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                    continue;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(130));
                // 入队到 OrderedQueue
                orderedQueue.enqueue(frame->meta.frame_id, std::move(frame));
                total_consumed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // ----------------- 顺序验证线程 -----------------
    uint64_t last_id = 0;
    bool first = true;

    while (running.load()) {
        FramePtr frame;
        // 使用小超时时间轮询
        if (!orderedQueue.try_dequeue(frame, 10)) {
            std::this_thread::sleep_for(std::chrono::microseconds(5));
            continue;
        }

        total_dequeued.fetch_add(1, std::memory_order_relaxed);

        if (!frame) continue;  // 可能因 timeout 跳帧

        uint64_t fid = frame->meta.frame_id;
        uint64_t expected_id = orderedQueue.get_expected_id();

        // 实时打印，同一行覆盖
        printf("\033[2K\rlast id: %lu | now id: %lu | expected id: %lu",
                last_id, fid, expected_id);
        fflush(stdout);
     

        if (first) {
            last_id = fid;
            first = false;
            continue;
        } 

        if (fid <= last_id) {
            std::cerr << "\n[ERROR] Non-increasing frame_id: "
                    << fid << " after " << last_id << std::endl;
            order_violation.store(true);
        } else if (fid != last_id + 1) {
            int64_t gap = fid - last_id - 1;
            std::cerr << "\n[WARN] Missing " << gap 
                    << " frame(s): got " << fid 
                    << " expected " << last_id + 1 << std::endl;
        }
        last_id = fid;
    }

    if (v4l2CapturerThread.joinable()) v4l2CapturerThread.join();
    // ----------------- 测试结果 -----------------

    orderedQueue.print_stats();
    std::cout << "Total produced: " << total_produced.load() << "\n"
              << "Total consumed: " << total_consumed.load() << "\n"
              << "Total dequeued: " << total_dequeued.load() << "\n"
              << "Is ordered: "     << order_violation.load() << std::endl;
    return 0;
}

int virSave(void *data, size_t buffer_size){
    // 保存为图像文件
    FILE* fp = fopen("output.rgba", "wb");
    if (nullptr == fp) {
        fprintf(stderr, "Failed to open output file");
        free(data);
        return -1;
    }
    fwrite(data, 1, buffer_size, fp);
    fclose(fp);

    // 释放内存
    free(data);
     
    return 0;
}

int rgaTest(){
    int ret = 0;
    // 创建队列
    auto rawFrameQueue  	= std::make_shared<FrameQueue>(2);
    auto frameQueue     	= std::make_shared<FrameQueue>(10);

    // 获取设备组合
    auto& devices = DrmDev::fd_ptr->getDevices();
    if (0 >= devices.size()){
        std::cout << "Get no devices." << std::endl;
        return -1;
    }
    // 取出第一个屏幕
    auto& dev = devices[0];
    std::cout << "Connector ID: " << dev->connector_id << ", CRTC ID: " << dev->crtc_id
    << ", Resolution: " << dev->width << "x" << dev->height << "\n";

    // 相机配置
    CameraController::Config cfg = {
        .buffer_count = 2,
        .plane_count = 2,
        .use_dmabuf = true,
        .device = "/dev/video0",
        .width = 1280,
        .height = 720,
        .format = V4L2_PIX_FMT_NV12
    };
    
    // 初始化相机控制器
    auto cctr         	= std::make_shared<CameraController>(cfg);
    if (!cctr) {
        std::cout << "Failed to create CameraController object.\n";
        return -1;
    }
    // 设置入队队列
    cctr->setFrameCallback([&rawFrameQueue](FramePtr f) {
        rawFrameQueue->enqueue(std::move(f));
    });

    // 根据格式转换 RGA 格式
    int format = (V4L2_PIX_FMT_NV12 == cfg.format) ?
    RK_FORMAT_YCbCr_420_SP : RK_FORMAT_YCrCb_422_SP;
    // 配置RGA参数
    RgaProcessor::Config rgacfg{
        cctr, rawFrameQueue, cfg.width,
        cfg.height, cfg.use_dmabuf, RK_FORMAT_RGBA_8888, format, 10
    };
    // 初始化转换线程
    RgaProcessor processor_(rgacfg) ;

    // 出队帧缓存
    FramePtr frame;
    cctr->start();
    processor_.start();

    while (1){
        
        if (processor_.dump(frame) < 0){
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        std::cout << "frame Index:\t" << frame->meta.index << "\nframe fd:\t"
            << frame->dmabuf_fd() << "\nw:\t" << frame->meta.w << "\nh:\t" << frame->meta.h
            << "\t\n---\n";
        processor_.releaseBuffer(frame->index());
    }
    // std::cout << "frame Index:\t" << frame->meta.index << "\nframe fd:\t"
    // << frame->dmabuf_fd() << "\nw:\t" << frame->meta.w << "\nh:\t" << frame->meta.h
    // << "\t\n---\n";
    // auto bufptr = frame->sharedState()->dmabuf_ptr;
    // RgaProcessor::dumpDmabufAsXXXX8888(bufptr->fd(), frame->meta.w, frame->meta.h, bufptr->size(), bufptr->pitch(), "./end.argb");

    processor_.releaseBuffer(frame->meta.index);
    processor_.stop();
    cctr->stop();
    return 0;
}

int dmabufTest() 
{   
    SafeQueue<DmaBufferPtr> queue_(8);

    for (int i = 0; i < 8; ++i)
    {
        DmaBufferPtr buf = DmaBuffer::create(1920, 1080, DRM_FORMAT_XRGB8888, 0, 0);
        queue_.enqueue(std::move(buf));
    }
    auto size = queue_.size();
    for (int i = 0; i < size; i++){
        auto buf = queue_.dequeue();
        if (nullptr != buf) {
            std::cout << "[rawDmabuf] Prime fd: " << buf->fd() << ", Size: " << buf->size()
                << ", Width: " << buf->width() << ", Height: " << buf->height() << std::endl;
        } else {
            std::cerr << "Failed to create DmaBuffer\n";
            continue;
        }
        // 从 fd 导入
        auto ibuf = DmaBuffer::importFromFD(buf->fd(), buf->width(), buf->height(), buf->format(), 0);
        if (nullptr != ibuf) {
            std::cout << "[importDmabuf] Prime fd: " << ibuf->fd() << ", Size: " << ibuf->size()
                << ", Width: " << ibuf->width() << ", Height: " << ibuf->height() << std::endl;
        } else {
            std::cerr << "Failed to import DmaBuffer from fd\n";
        }
    }
    return 0;
}

int layerTest(){
    auto& devices = DrmDev::fd_ptr->getDevices();
    if (devices.empty()) {
        std::cout << "Get no devices." << std::endl;
        return -1;
    }
    
    auto& dev = devices[0];
    if (0 == dev->connector_id) return -1;
    std::cout << "Connector ID: " << dev->connector_id << ", CRTC ID: " << dev->crtc_id
    << ", Resolution: " << dev->width << "x" << dev->height << "\n";
    
    DmaBufferPtr dmabuf = DmaBuffer::create(dev->width, dev->height, DRM_FORMAT_XRGB8888, 0);

    DrmLayer layer({dmabuf}, 2);
    auto fbid = layer.getProperty("fbId").get<uint32_t>();
    std::cout << "FramebufferId: " << fbid << "\n";
    int ret = drmModeSetCrtc(DrmDev::fd_ptr->get(), 
                         dev->crtc_id, 
                         fbid,  
                         0, 0,
                         &dev->connector_id, 1, 
                         &dev->mode); // <-- 传指针

    std::cout << "ret : " << ret << "\n";
    // while(1); // 热插拔事件检测
    return ret;
}

int drmDevicesControllerTest(){
    auto fd = DrmDev::fd_ptr;
    // 测试各项资源获取
    auto res = fd->getResources();
    auto planeRes = fd->getPlaneResources();
    if (!res || !planeRes) {
        std::cout << "Get resources faild\n";
        return -1;
    }
    std::cout << "From resources Get " << res->count_connectors << " connectors, "
    << res->count_encoders << " encoders, "
    << res->count_crtcs << " crtcs.\n";

    auto& devices = fd->getDevices();
    std::cout << "Get " << devices.size() << " devices combinations.\n";
    if (devices.empty()){
        std::cout << "Gets no device combined.\n"; 
        return -1;
    }
    
   size_t planeCount;
    for (const auto& dev : devices) {
        std::cout << "Connector ID: " << dev->connector_id << ", CRTC ID: " << dev->crtc_id
        << ", Resolution: " << dev->width << "x" << dev->height << "\n";
        // 通过格式筛选
        planeCount = fd->refreshPlane(dev->crtc_id);
        std::cout << "Find " << planeCount << " matched planes.\n\n";
    }
    // 通过类型筛选
    std::vector<uint32_t> getsPlanesIds;
    fd->getPossiblePlane(DRM_PLANE_TYPE_OVERLAY, DRM_FORMAT_RGB888, getsPlanesIds);
    std::cout << "Find " << getsPlanesIds.size() << " matched OVERLAY planes.\n";
    if (getsPlanesIds.empty()) return -1;
    
    for (auto& id : getsPlanesIds){
        auto planeCache = fd->getPlaneById(id);
        if (!planeCache) {
            std::cout << "There is no plane for ID: " << id << "\n";
            continue;
        }
        std::cout << "Find Plane: " << id << ", Plane supported formats: \n";
        for (auto& format : planeCache->formats){
            std::cout << fourccToString(format) << ", \n";
        }
    }
    // 测试热插拔
    return 0;
}

int mouseTest(){
    MouseWatcher mouse;
    mouse.setScreenSize(1280, 720);
    mouse.start();

    std::cout << "Mouse Test started. Move the mouse or click buttons to see events.\n";
    std::cout << "Press Ctrl+C to stop.\n";
    int x = 0;
    int y = 0;
    mouse.registerHandler({BTN_LEFT}, [](uint16_t ty, uint8_t val){
        std::cout << "\nLeft button clicked!\n";
    });
    mouse.registerHandler({BTN_RIGHT}, [](uint16_t ty, uint8_t val){
        std::cout << "\nRight button clicked!\n";
    });
    while (running) {
        mouse.getPosition(x, y);
        std::cout << "\rMouse Position: (" << x << ", " << y << ")   \n" << std::flush;
    }
    mouse.stop();
    std::cout << "Mouse Test stopped.\n" << std::endl;
    return 0;
}

int resourceMonitorTest(){
    CpuMonitor cpu_monitor(500);  // 500ms 采样间隔
    MemoryMonitor mem_monitor(500);

    float mem_usage = 0.0f;
    float cpu_usage = 0.0f;

    // 初始化清屏，设置起始位置
    fprintf(stdout, "\033[2J\033[H"); // 清屏并移到左上角
    fflush(stdout);

    while (running) {
        cpu_usage = cpu_monitor.getUsage();
        mem_usage = mem_monitor.getUsage();

        // 移动光标到第一行，覆盖刷新
        fprintf(stdout, "\033[1;1H"); // 移动到第1行第1列
        fprintf(stdout, "CPU usage:  %.1f%%\033[K\n", cpu_usage); // 清行尾
        fprintf(stdout, "Memory usage: %.1f%%\033[K", mem_usage); // 无换行，清行尾
        fflush(stdout); // 立即刷新输出

        sleep(1);
    }

    // 退出时清屏
    fprintf(stdout, "\033[2J\033[H");
    fflush(stdout);
    return 0;
}

int main(int argc, char const *argv[]) {
    DrmDev::fd_ptr = DeviceController::create();
    if (!DrmDev::fd_ptr) {
        std::cout << "Init DrmDev::fd_ptr faild\n";
        return -1;
    }
    int ret = 0;
    std::signal(SIGINT, handleSignal);

    // 定义测试用例映射表
    // key: 命令行参数 (如 "--rgatest")
    // value: 一个无参数且返回 int 的函数对象（可以是函数指针、lambda等）
    std::unordered_map<std::string, std::function<int()>> testMap = {
        {"--mpmctest", mpmcTestMaxPerf},
        {"--spsctest", spscTestMaxPerf},
        {"--orderedtest", orderedQueueTest},
        {"--rgatest", rgaTest},    // 直接使用函数指针
        {"--dmatest", dmabufTest},
        {"--layertest", layerTest},
        {"--mousetest", mouseTest},
        {"--devtest", drmDevicesControllerTest},
        {"--restest", resourceMonitorTest},
        {"--ctxtest", ENCTest::contextInit},
        {"--coretest", ENCTest::coreTest},
        {"--RGA_coretest", ENCTest::rgaCopy_coreTest},
        {"--streamtest", ENCTest::streamTest},
        {"--recordtest", ENCTest::cameraRecordTest},
        {"--fbshow", [](){ 
            FrameBufferTest test;
            test.start();
            while(running){ sleep(1000); }
            test.stop();
            return 0;
        }} // 也可以用 Lambda 包装
    };

    const std::string help_opt = "--help";

    // 处理帮助信息或无参数情况
    if (argc == 1 || std::string(argv[1]) == help_opt) {
        std::cout << "用法: " << argv[0] << " [选项]" << std::endl;
        std::cout << "选项:" << std::endl;
        for (const auto& pair : testMap) { // 遍历map，打印所有选项:cite[1]:cite[2]
            std::cout << "  " << pair.first << "   运行对应测试" << std::endl;
        }
        std::cout << "  " << help_opt << "     显示此帮助信息" << std::endl;
        return 0;
    }

    std::string inputArg = argv[1]; // 获取用户输入的参数

    // 在map中查找输入的参数
    auto it = testMap.find(inputArg);
    if (it != testMap.end()) {
        // 找到了对应的测试函数
        try {
            ret = it->second(); // 执行找到的函数
        } catch (const std::exception& e) {
            std::cerr << "运行时错误: " << e.what() << std::endl;
            ret = 1;
        } catch (...) {
            std::cerr << "未知错误发生" << std::endl;
            ret = 1;
        }
    } else {
        // 没有找到对应的命令
        std::cerr << "未知选项: " << inputArg << std::endl;
        std::cerr << "请使用 '" << help_opt << "' 查看可用选项。" << std::endl;
        return 1;
    }

    return ret;
}