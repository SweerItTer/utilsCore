/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-11-17 20:29:28
 * @FilePath: /EdgeVision/examples/encoderTest.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "mpp/encoderCore.h"
#include "mpp/streamWriter.h"
#include "rga/rgaConverter.h"
#include "mpp/jpegEncoder.h"

#include <numeric>
#include <algorithm>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

using namespace std::chrono;
    
struct RecordPerfStats {
    std::vector<int64_t> slot_acquire_us;      // 获取slot耗时
    std::vector<int64_t> frame_dequeue_us;     // 帧出队耗时
    std::vector<int64_t> rga_copy_us;          // RGA拷贝耗时
    std::vector<int64_t> encode_submit_us;     // 提交编码耗时
    std::vector<int64_t> write_push_us;        // 写入推送耗时
    std::vector<int64_t> loop_total_us;        // 单次循环总耗时
    
    int64_t total_time_ms = 0;
    int frame_count = 0;
    int slot_acquire_fail = 0;
    int frame_dequeue_fail = 0;
    
    // 计算统计值
    static std::tuple<int64_t, int64_t, int64_t, int64_t, int64_t> 
    calc_stats(const std::vector<int64_t>& data) {
        if (data.empty()) return std::make_tuple(0, 0, 0, 0, 0);
        auto sorted = data;
        std::sort(sorted.begin(), sorted.end());
        int64_t sum = std::accumulate(sorted.begin(), sorted.end(), 0LL);
        
        int64_t avg = sum / sorted.size();
        int64_t median = sorted[sorted.size() / 2];
        int64_t min = sorted[0];
        int64_t max = sorted[sorted.size() - 1];
        int64_t p95 = sorted[sorted.size() * 95 / 100];
        
        return std::make_tuple(avg, median, min, max, p95);
    }
    
    static void print_stat(const char* name, const std::vector<int64_t>& data) {
        if (data.empty()) {
            std::cout << "\n[" << name << "] 无数据\n";
            return;
        }
        
        auto stats = calc_stats(data);
        int64_t avg = std::get<0>(stats);
        int64_t median = std::get<1>(stats);
        int64_t min = std::get<2>(stats);
        int64_t max = std::get<3>(stats);
        int64_t p95 = std::get<4>(stats);
        
        std::cout << "\n[" << name << "]\n"
                  << "  平均: " << std::setw(8) << avg << " μs\n"
                  << "  中位: " << std::setw(8) << median << " μs\n"
                  << "  最小: " << std::setw(8) << min << " μs\n"
                  << "  最大: " << std::setw(8) << max << " μs\n"
                  << "  P95:  " << std::setw(8) << p95 << " μs\n";
    }
    
    void print() {
        if (frame_count == 0) return;
        
        std::cout << "\n\n========== 录制性能测试报告 ==========\n";
        std::cout << "成功帧数: " << frame_count << "\n";
        std::cout << "失败次数: slot获取=" << slot_acquire_fail 
                  << ", 帧出队=" << frame_dequeue_fail << "\n";
        std::cout << "总耗时:   " << total_time_ms << " ms\n";
        std::cout << "平均帧率: " << std::fixed << std::setprecision(2) 
                  << (frame_count * 1000.0 / total_time_ms) << " fps\n";
        
        print_stat("Slot获取", slot_acquire_us);
        print_stat("帧出队", frame_dequeue_us);
        print_stat("RGA拷贝", rga_copy_us);
        print_stat("编码提交", encode_submit_us);
        print_stat("写入推送", write_push_us);
        print_stat("单帧总耗时", loop_total_us);
        
        // 瓶颈分析
        std::cout << "\n瓶颈分析:\n";
        auto stats_slot = calc_stats(slot_acquire_us);
        auto stats_deq = calc_stats(frame_dequeue_us);
        auto stats_rga = calc_stats(rga_copy_us);
        auto stats_enc = calc_stats(encode_submit_us);
        auto stats_write = calc_stats(write_push_us);
        
        int64_t avg_slot = std::get<0>(stats_slot);
        int64_t avg_deq = std::get<0>(stats_deq);
        int64_t avg_rga = std::get<0>(stats_rga);
        int64_t avg_enc = std::get<0>(stats_enc);
        int64_t avg_write = std::get<0>(stats_write);
        
        int64_t total = avg_slot + avg_deq + avg_rga + avg_enc + avg_write;
        
        std::cout << "  Slot获取:  " << std::setw(6) << avg_slot << " μs (" 
                  << std::setw(5) << std::setprecision(1) 
                  << (avg_slot * 100.0 / total) << "%)\n";
        std::cout << "  帧出队:    " << std::setw(6) << avg_deq << " μs (" 
                  << std::setw(5) << (avg_deq * 100.0 / total) << "%)\n";
        std::cout << "  RGA拷贝:   " << std::setw(6) << avg_rga << " μs (" 
                  << std::setw(5) << (avg_rga * 100.0 / total) << "%)\n";
        std::cout << "  编码提交:  " << std::setw(6) << avg_enc << " μs (" 
                  << std::setw(5) << (avg_enc * 100.0 / total) << "%)\n";
        std::cout << "  写入推送:  " << std::setw(6) << avg_write << " μs (" 
                  << std::setw(5) << (avg_write * 100.0 / total) << "%)\n";
        
        // 建议
        std::cout << "\n优化建议:\n";
        if (avg_rga > 5000) {
            std::cout << "  ⚠️  RGA拷贝耗时较长 (>" << avg_rga/1000 << "ms)\n"
                      << "     考虑直接用摄像头输出目标分辨率\n";
        }
        if (slot_acquire_fail > frame_count * 0.1) {
            std::cout << "  ⚠️  Slot获取失败率过高 (" 
                      << (slot_acquire_fail * 100.0 / (frame_count + slot_acquire_fail)) 
                      << "%)\n"
                      << "     考虑增加SLOT_COUNT\n";
        }
        if (avg_enc > 1000) {
            std::cout << "  ⚠️  编码提交耗时较长,可能VPU队列已满\n";
        }
        
        std::cout << "======================================\n";
    }
};
struct CapturePerfStats {
    std::vector<int64_t> frame_dequeue_us;
    std::vector<int64_t> jpeg_encode_us;
    std::vector<int64_t> end_to_end_us;
    int64_t total_time_ms = 0;
    int frame_count = 0;
    
    // 修复: 使用静态函数或普通函数,避免捕获 this
    static std::tuple<int64_t, int64_t, int64_t, int64_t, int64_t> 
    calc_stats(const std::vector<int64_t>& data) {
        auto sorted = data;
        std::sort(sorted.begin(), sorted.end());
        int64_t sum = std::accumulate(sorted.begin(), sorted.end(), 0LL);
        
        int64_t avg = sum / sorted.size();
        int64_t median = sorted[sorted.size() / 2];
        int64_t min = sorted[0];
        int64_t max = sorted[sorted.size() - 1];
        int64_t p95 = sorted[sorted.size() * 95 / 100];
        
        return std::make_tuple(avg, median, min, max, p95);
    }
    
    static void print_stat(const char* name, const std::vector<int64_t>& data) {
        // 修复: 不使用结构化绑定
        auto stats = calc_stats(data);
        int64_t avg = std::get<0>(stats);
        int64_t median = std::get<1>(stats);
        int64_t min = std::get<2>(stats);
        int64_t max = std::get<3>(stats);
        int64_t p95 = std::get<4>(stats);
        
        std::cout << "\n[" << name << "]\n"
                    << "  平均: " << avg << " μs\n"
                    << "  中位: " << median << " μs\n"
                    << "  最小: " << min << " μs\n"
                    << "  最大: " << max << " μs\n"
                    << "  P95:  " << p95 << " μs\n";
    }
    
    void print() {
        if (frame_count == 0) return;
        
        std::cout << "\n========== 性能测试报告 ==========\n";
        std::cout << "总帧数: " << frame_count << "\n";
        std::cout << "总耗时: " << total_time_ms << " ms\n";
        std::cout << "平均吞吐: " << std::fixed << std::setprecision(2) 
                    << (frame_count * 1000.0 / total_time_ms) << " fps\n";
        
        print_stat("帧出队", frame_dequeue_us);
        print_stat("JPEG编码(含IO)", jpeg_encode_us);
        print_stat("端到端延迟", end_to_end_us);
        
        std::cout << "\n瓶颈分析:\n";
        
        // 修复: 不使用结构化绑定
        auto deq_stats = calc_stats(frame_dequeue_us);
        auto enc_stats = calc_stats(jpeg_encode_us);
        int64_t avg_deq = std::get<0>(deq_stats);
        int64_t avg_enc = std::get<0>(enc_stats);
        
        double deq_pct = avg_deq * 100.0 / (avg_deq + avg_enc);
        double enc_pct = avg_enc * 100.0 / (avg_deq + avg_enc);
        
        std::cout << "  帧出队占比: " << std::fixed << std::setprecision(1) 
                    << deq_pct << "%\n";
        std::cout << "  编码占比:   " << enc_pct << "%\n";
        
        if (avg_enc > 100000) {  // > 100ms
            std::cout << "\n!!!  编码耗时过长,建议:\n"
                        << "  1. 降低JPEG质量(当前8→6)\n"
                        << "  2. 降低分辨率(4K→1080p)\n"
                        << "  3. 检查VPU时钟频率\n";
        }
        std::cout << "==================================\n";
    }
};

namespace ENCTest{
/// 默认配置 
inline static MppEncoderContext::Config initConfig() {
    return DefaultConfigs::defconfig_720p_video();
}

/// 填充测试图像数据
inline static bool fillBuffer(DmaBufferPtr dmaSrc, int frame_idx){
    uint8_t* base = static_cast<uint8_t*>(dmaSrc->map());
    if (!base) {
        std::cerr << "map 失败！\n";
        return false;
    }
    
    size_t width = dmaSrc->width();
    size_t height = dmaSrc->height();
    size_t pitch = dmaSrc->pitch();
    
    size_t y_size = pitch * height;

    uint8_t* y_plane  = base;
    uint8_t* uv_plane = base + y_size;

    // 颜色切换
    uint8_t y_val, u_val, v_val;
    if (frame_idx % 90 < 30) { y_val = 76;  u_val = 90;  v_val = 240; }       // 红
    else if (frame_idx % 90 < 60) { y_val = 150; u_val = 43;  v_val = 22; }   // 绿
    else { y_val = 29; u_val = 225; v_val = 110; }                            // 蓝

    // Y 平面
    for (int h = 0; h < height; ++h) {
        memset(y_plane + h * width, y_val, width);
    }

    // UV 平面
    for (int h = 0; h < height / 2; ++h) {
        uint8_t* line = uv_plane + h * width;
        for (int w = 0; w < width; w += 2) {
            line[w]     = u_val;
            line[w + 1] = v_val;
        }
    }
    dmaSrc->unmap();
    return true;
}
/// 单线程堵塞保存编码包到文件
inline static bool packetSave(FILE* fp, MppEncoderCore::EncodedMeta& meta, bool showInfo = true) {
    auto print = [showInfo](const std::string& str){
        if (!showInfo) return;
        std::cout << str << std::endl; 
    };
    int tries = 200;
    while (--tries) {
        bool isEncoded = meta.core->tryGetEncodedPacket(meta);
        if (!isEncoded) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            continue;
        }
        auto packet = meta.packet;
        if (!packet){
            print("编码失败\n");
            continue;
        }
        print("开始写入\n");
        // 写入文件
        void * data = packet->data();
        if (!data){
            print("mpp_packet_get_data 返回为空\n");
            fclose(fp);
            return -1;
        }
        fwrite(data, 1, packet->length(), fp);
        fflush(fp);

        std::string info = "编码成功 | " + std::to_string(packet->length()) + " 字节 | " + (packet->isKeyframe() ? "I帧" : "P帧") + "\n";
        print(info);
        break;
    }
    if (tries >= 1000) {
        print("超时！\n");
        return false;
    }
    return true;
}   

/**
 * @brief 上下文测试
 */
int contextInit() {
    MppEncoderContext::Config cfg = initConfig();
    
    // 创建编码器上下文
    MppEncoderContext encoder(cfg);

    // 验证上下文是否创建成功
    if (nullptr == encoder.ctx() || nullptr == encoder.api() || nullptr == encoder.encCfg()) {
        std::cerr << "[Test] Encoder context initialization failed!" << std::endl;
        return -1;
    } else {
        std::cout << "[Test] Encoder context created successfully." << std::endl;
    }

    // 测试修改配置
    MppEncoderContext::Config newCfg = cfg;
    newCfg.prep_width = 1920;
    newCfg.prep_height = 1080;
    newCfg.rc_bps_target = 4 * 1024 * 1024;

    if (encoder.resetConfig(newCfg)) {
        std::cout << "[Test] Encoder configuration reset successfully." << std::endl;
    } else {
        std::cerr << "[Test] Failed to reset encoder configuration." << std::endl;
        return -1;
    }

    // 可选: 读取几个配置项确认
    int width = 0, height = 0;
    mpp_enc_cfg_get_s32(encoder.encCfg(), "prep:width", &width);
    mpp_enc_cfg_get_s32(encoder.encCfg(), "prep:height", &height);

    std::cout << "[Test] Current width: " << width << ", height: " << height << std::endl;

    return 0;
}

/**
 * @brief 编码测试
 */
int coreTest() {
    auto cfg = initConfig();

    MppEncoderCore core(cfg, 0);

    FILE* fp = fopen("test_720p_contiguous_nv12.h264", "wb");
    if (!fp) { std::cerr << "打开文件失败\n"; return -1; }

    std::cout << "=== RK356x 连续 NV12 压测开始 ===\n";

    auto for_test = [&](){
        core.resetConfig(cfg);
        for (int frame_idx = 0; frame_idx < 200; ++frame_idx) {
            auto tmp = core.acquireWritableSlot();
            auto dmabuf = tmp.first;
            auto slot_id = tmp.second;
            if (!dmabuf || slot_id < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            SlotGuard guard(&core, slot_id);
    
            if (false == fillBuffer(dmabuf, frame_idx)) {
                std::cerr << "填充 失败！\n";
                continue;
            }
            auto meta = core.submitFilledSlot(slot_id);
            // std::cout << "填充完成" << std::endl;
            if (!packetSave(fp, meta, false)) std::cerr << "编码失败.\n";
        }
        core.endOfthisEncode();
    };

    for (int i = 0; i < 5; ++i) {
        for_test();
    }

    fclose(fp);
    std::cout << "\n搞定！播放命令：\n";
    std::cout << "ffplay test_720p_contiguous_nv12.h264\n";
    return 0;
}

/**
 * @brief 带RGA拷贝的编码测试
 */
int rgaCopy_coreTest(){
    auto& converter = RgaConverter::instance();
    auto cfg = initConfig();
    MppEncoderCore core(cfg, 0);
    
    FILE* fp = fopen("test_rga-cpoy_720p_nv12.h264", "wb");
    if (!fp) { std::cerr << "打开文件失败\n"; return -1; }

    auto dmaSrc = DmaBuffer::create(1920, 1080, DRM_FORMAT_NV12, 0, 0);
    if (!dmaSrc) {
        std::cerr << "创建 DAMBUF 失败！\n";
        return -1;
    }
    
    rga_buffer_t src = wrapbuffer_fd(dmaSrc->fd(), dmaSrc->width(), dmaSrc->height(), RK_FORMAT_YCbCr_420_SP);
    src.wstride = dmaSrc->pitch();
    src.hstride = dmaSrc->height();
    im_rect srcR{0,0,static_cast<int>(dmaSrc->pitch()), static_cast<int>(dmaSrc->height())};

    int frame_idx = 100;
    while (--frame_idx) {
        auto tmp = core.acquireWritableSlot();
        auto dmaDst = tmp.first;
        auto slot_id = tmp.second;
        if (slot_id < 0){
            std::cerr << "获取可用 slot 失败！\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        } else if (!dmaDst){
            std::cerr << "获取可用 dmabuf 失败！\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        rga_buffer_t dst = wrapbuffer_fd(dmaDst->fd(), dmaDst->width(), dmaDst->height(), RK_FORMAT_YCbCr_420_SP);
        dst.wstride = dmaDst->pitch();
        dst.hstride = dmaDst->height();
        im_rect dstR{0,0,static_cast<int>(dmaDst->pitch()), static_cast<int>(dmaDst->height())};
        RgaConverter::RgaParams param{
            .src = src,
            .src_rect=srcR,
            .dst=dst,
            .dst_rect=dstR
        };
        
        SlotGuard guard(&core, slot_id);

        if (false == fillBuffer(dmaSrc, frame_idx)) {
            std::cerr << "填充 失败！\n";
            continue;
        }

        if(converter.ImageResize(param) != IM_STATUS_SUCCESS){
            std::cerr << "RGA-copy 失败！\n";
            continue;
        }
        auto meta = core.submitFilledSlot(slot_id);
        if (!packetSave(fp, meta)) std::cerr << "编码失败.\n";
    }
    
    fclose(fp);
    std::cout << "\n搞定！播放命令：\n";
    std::cout << "ffplay test_rga-cpoy_720p_nv12.h264\n";
    return 0;
}

/**
 * @brief 流式编码测试
 */
int streamTest(){
    MppEncoderContext::Config cfg = initConfig();
    RgaConverter& converter = RgaConverter::instance();

    MppEncoderCore core(cfg, 0);
    StreamWriter writer("stream_test_720p_nv12.h264");

    auto dmaSrc = DmaBuffer::create(1920, 1080, DRM_FORMAT_NV12, 0, 0);
    if (!dmaSrc) {
        std::cerr << "创建 DAMBUF 失败！\n";
        return -1;
    }
    rga_buffer_t src = wrapbuffer_fd(dmaSrc->fd(), dmaSrc->width(), dmaSrc->height(), RK_FORMAT_YCbCr_420_SP);
    src.wstride = dmaSrc->pitch();
    src.hstride = dmaSrc->height();
    im_rect srcR{0,0,static_cast<int>(dmaSrc->pitch()), static_cast<int>(dmaSrc->height())};

    std::cout << "=== RK356x 流式 NV12 编码测试开始 ===\n";
    for (int frame_idx = 0; frame_idx < 1210; ++frame_idx) {
        auto tmp = core.acquireWritableSlot();
        auto dmaDst = tmp.first;
        auto slot_id = tmp.second;
        if (!dmaDst || slot_id < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            std::cerr << "获取可用 slot 失败！\n";
            continue;
        }

        rga_buffer_t dst = wrapbuffer_fd(dmaDst->fd(), dmaDst->width(), dmaDst->height(), RK_FORMAT_YCbCr_420_SP);
        dst.wstride = dmaDst->pitch();
        dst.hstride = dmaDst->height();
        im_rect dstR{0,0,static_cast<int>(dmaDst->pitch()), static_cast<int>(dmaDst->height())};
        RgaConverter::RgaParams param{
            .src = src,
            .src_rect=srcR,
            .dst=dst,
            .dst_rect=dstR
        };
        
        SlotGuard guard(&core, slot_id);

        if (false == fillBuffer(dmaSrc, frame_idx)) {
            std::cerr << "填充 失败！\n";
            continue;
        }

        if(converter.ImageResize(param) != IM_STATUS_SUCCESS){
            std::cerr << "RGA-copy 失败！\n";
            continue;
        }
        auto meta = core.submitFilledSlot(slot_id);

        guard.release(); // 提前释放 slot
        writer.pushMeta(meta);
    }

    writer.stop();
    std::cout << "\n搞定！播放命令：\n";
    std::cout << "ffplay stream_test_720p_nv12.h264\n";
    return 0;
}

int cameraRecordTest(){
    RecordPerfStats perf;
    static std::atomic_bool running{true};
    auto handleSignal = [](int signal) {
        if (signal == SIGINT) {
            std::cout << "\nCtrl+C received, stopping...\n";
            running.store(false);
        }
    };
    std::signal(SIGINT, handleSignal);

    // 创建队列
    std::shared_ptr<FrameQueue> rawFrameQueue = std::make_shared<FrameQueue>(2);
    
    // 配置初始化
    auto cfg = DefaultConfigs::defconfig_1080p_video(30);
    
    auto width = cfg.prep_width;
    auto height = cfg.prep_height;
    
    std::cout << "\n=== 录制性能测试配置 ===\n";
    std::cout << "分辨率: " << width << "x" << height << "\n";
    std::cout << "目标帧率: "<< cfg.rc_fps_in_num <<" fps\n";
    std::cout << "编码器: H.264\n";
    std::cout << "测试时长: 按Ctrl+C停止\n\n";
    
    CameraController::Config cctrCfg{
        .buffer_count = 2,
        .plane_count = 2,
        .use_dmabuf = true,
        .device = "/dev/video0",
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .format = V4L2_PIX_FMT_NV12
    };
    
    // 实例化
    std::cout << "初始化组件...\n";
    auto init_start = steady_clock::now();
    
    RgaConverter& converter = RgaConverter::instance();
    std::shared_ptr<CameraController> cctr = std::make_shared<CameraController>(cctrCfg);
    MppEncoderCore core(cfg, 0);
    StreamWriter writer("camera_record_nv12.h264");
    
    auto init_end = steady_clock::now();
    std::cout << "初始化耗时: " 
              << duration_cast<milliseconds>(init_end - init_start).count() 
              << " ms\n";
    
    // 摄像头回调
    cctr->setFrameCallback([&](FramePtr f) { 
        rawFrameQueue->enqueue(std::move(f));
    });
    
    std::cout << "\n=== 开始录制 ===\n";
    std::cout << "提示: 按 Ctrl+C 停止录制\n\n";
    
    // 启动摄像头采集
    cctr->start();
    cctr->setThreadAffinity(2);
    
    auto record_start = steady_clock::now();
    int frame_idx = 0;
    
    // 统计间隔
    const int REPORT_INTERVAL = 30;  // 每30帧输出一次实时统计
    
    while (running) {
        auto loop_start = steady_clock::now();
        
        // ============ 测试点 1: 获取 slot ============
        auto t1 = steady_clock::now();
        auto tmp = core.acquireWritableSlot();
        auto slotDma = tmp.first;
        auto slot_id = tmp.second;
        auto t2 = steady_clock::now();
        
        if (!slotDma || slot_id < 0) {
            perf.slot_acquire_fail++;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        
        perf.slot_acquire_us.push_back(duration_cast<microseconds>(t2 - t1).count());
        
        // 使用 SlotGuard 确保 slot 释放
        SlotGuard guard(&core, slot_id);

        // ============ 测试点 2: 获取帧 ============
        auto t3 = steady_clock::now();
        FramePtr frame;
        if (!rawFrameQueue->try_dequeue(frame)) continue;
        auto t4 = steady_clock::now();
        
        if (!frame) {
            perf.frame_dequeue_fail++;
            continue;
        }
        perf.frame_dequeue_us.push_back(duration_cast<microseconds>(t4 - t3).count());
        
        // 获取 DMA buffer
        auto dmaSrc = frame->sharedState(0)->dmabuf_ptr;
        if (!dmaSrc) {
            perf.frame_dequeue_fail++;
            continue;
        }
        
        // ============ 测试点 3: RGA 拷贝 ============
        auto t5 = steady_clock::now();
        
        rga_buffer_t src = wrapbuffer_fd(dmaSrc->fd(), dmaSrc->width(), 
                                         dmaSrc->height(), formatDRMtoRGA(dmaSrc->format()));
        src.wstride = dmaSrc->pitch();
        src.hstride = dmaSrc->height();
        im_rect srcR{0, 0, static_cast<int>(dmaSrc->width()), static_cast<int>(dmaSrc->height())};

        rga_buffer_t dst = wrapbuffer_fd(slotDma->fd(), slotDma->width(), 
                                         slotDma->height(), formatDRMtoRGA(slotDma->format()));
        dst.wstride = slotDma->pitch();
        dst.hstride = slotDma->height();
        im_rect dstR{0, 0, static_cast<int>(slotDma->pitch()), static_cast<int>(slotDma->height())};
        
        RgaConverter::RgaParams param{
            .src = src,
            .src_rect = srcR,
            .dst = dst,
            .dst_rect = dstR
        };
        
        if(converter.ImageResize(param) != IM_STATUS_SUCCESS){
            std::cerr << "\n[ERROR] RGA拷贝失败！\n";
            return -1;
        }
        
        auto t6 = steady_clock::now();
        perf.rga_copy_us.push_back(duration_cast<microseconds>(t6 - t5).count());
        
        // ============ 测试点 4: 提交编码 ============
        auto t7 = steady_clock::now();
        auto meta = core.submitFilledSlot(slot_id);
        auto t8 = steady_clock::now();
        perf.encode_submit_us.push_back(duration_cast<microseconds>(t8 - t7).count());
        
        // ============ 测试点 5: 写入推送 ============
        auto t9 = steady_clock::now();
        guard.release(); 
        writer.pushMeta(meta);
        auto t10 = steady_clock::now();
        perf.write_push_us.push_back(duration_cast<microseconds>(t10 - t9).count());
        
        // 循环总耗时
        auto loop_end = steady_clock::now();
        auto loop_us = duration_cast<microseconds>(loop_end - loop_start).count();
        perf.loop_total_us.push_back(loop_us);
        
        perf.frame_count++;
        frame_idx++;
        
        // 实时进度
        if (frame_idx % REPORT_INTERVAL == 0) {
            auto current = steady_clock::now();
            auto elapsed_ms = duration_cast<milliseconds>(current - record_start).count();
            double current_fps = perf.frame_count * 1000.0 / elapsed_ms;
            
            // 最近 30 帧的平均耗时
            size_t recent_start = perf.loop_total_us.size() > REPORT_INTERVAL 
                                  ? perf.loop_total_us.size() - REPORT_INTERVAL : 0;
            int64_t recent_sum = 0;
            for (size_t i = recent_start; i < perf.loop_total_us.size(); ++i) {
                recent_sum += perf.loop_total_us[i];
            }
            int64_t recent_avg = recent_sum / (perf.loop_total_us.size() - recent_start);
            
            std::cout << "\r[" << frame_idx << " 帧] "
                      << "当前: " << std::fixed << std::setprecision(1) << current_fps << " fps, "
                      << "最近: " << recent_avg << " μs/帧"
                      << std::flush;
        }
    }
    
    auto record_end = steady_clock::now();
    perf.total_time_ms = duration_cast<milliseconds>(record_end - record_start).count();
    
    std::cout << "\n\n停止录制...\n";
    core.endOfthisEncode();
    writer.stop();
    cctr->stop();
    converter.deinit();
    
    // ============ 打印详细报告 ============
    perf.print();
    
    // ============ 文件信息 ============
    std::cout << "\n========== 输出文件 ==========\n";
    system("ls -lh camera_record_nv12.h264");
    
    std::cout << "\n播放命令:\n";
    std::cout << "  ffplay camera_record_nv12.h264\n";   
    std::cout << "\n拉取命令:\n";
    std::cout << "  adb pull /camera_record_nv12.h264 .\n"; 
    
    return 0;    
}

int cameraRawPiplineRecordTest(){
    static std::atomic_bool running{true};
    auto handleSignal = [](int signal) {
        if (signal == SIGINT) {
            std::cout << "\nCtrl+C received, stopping...\n";
            running.store(false);
        }
    };
    std::signal(SIGINT, handleSignal);

    std::shared_ptr<FrameQueue> rawFrameQueue = std::make_shared<FrameQueue>(2);

    auto cfg = DefaultConfigs::defconfig_1080p_video(30);
    auto width = cfg.prep_width;
    auto height = cfg.prep_height;

    std::cout << "\n=== 录制配置 ===\n";
    std::cout << "分辨率: " << width << "x" << height << "\n";
    std::cout << "目标帧率: " << cfg.rc_fps_in_num << " fps\n";
    std::cout << "编码器: H.264\n";
    std::cout << "按 Ctrl+C 停止\n\n";

    CameraController::Config cctrCfg{
        .buffer_count = 2,
        .plane_count = 2,
        .use_dmabuf = true,
        .device = "/dev/video0",
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .format = V4L2_PIX_FMT_NV12
    };

    std::cout << "初始化组件...\n";
    auto init_start = steady_clock::now();

    std::shared_ptr<CameraController> cctr = std::make_shared<CameraController>(cctrCfg);
    MppEncoderCore core(cfg, 0);
    StreamWriter writer("camera_record_nv12.h264");

    auto init_end = steady_clock::now();
    std::cout << "初始化耗时: " 
              << duration_cast<milliseconds>(init_end - init_start).count() 
              << " ms\n";

    cctr->setFrameCallback([&](FramePtr f) { 
        rawFrameQueue->enqueue(std::move(f));
    });

    std::cout << "\n=== 开始录制 ===\n";
    cctr->start();
    cctr->setThreadAffinity(2);

    while (running) {
        auto tmp = core.acquireWritableSlot();
        auto slotDma = tmp.first;
        auto slot_id = tmp.second;

        if (!slotDma || slot_id < 0) continue;

        SlotGuard guard(&core, slot_id);

        FramePtr frame;
        if (!rawFrameQueue->try_dequeue(frame)) continue;
        if (!frame) continue;

        auto dmaSrc = frame->sharedState(0)->dmabuf_ptr;
        if (!dmaSrc) continue;

        guard.release(); 
        writer.pushMeta(core.submitFilledSlotWithExternal(slot_id, dmaSrc, frame));
    }

    std::cout << "\n停止录制...\n";
    core.endOfthisEncode();
    writer.stop();
    cctr->stop();

    std::cout << "\n======= 输出文件 =======\n";
    system("ls -lh camera_record_nv12.h264");

    std::cout << "\n播放: ffplay camera_record_nv12.h264\n";
    std::cout << "拉取: adb pull /camera_record_nv12.h264 .\n";

    return 0;    
}

int jpegCaptureTest(){
    CapturePerfStats perf;
    // 创建队列
    std::shared_ptr<FrameQueue> rawFrameQueue = std::make_shared<FrameQueue>(2);
    
    auto cameraSetUp = [&rawFrameQueue](CameraController::Config& cctrCfg){
        std::shared_ptr<CameraController> cctr = std::make_shared<CameraController>(cctrCfg);
        cctr->setFrameCallback([&](FramePtr f) { 
            rawFrameQueue->enqueue(std::move(f));
        });
        return cctr;
    };
    
    // 初始化 JPEG 编码器
    JpegEncoder::Config jpeg_cfg;
    jpeg_cfg.width = 3840;
    jpeg_cfg.height = 2160;
    jpeg_cfg.format = MPP_FMT_YUV420SP;
    jpeg_cfg.quality = 8;
    jpeg_cfg.save_dir = "/tmp/photos";
    
    auto width = jpeg_cfg.width;
    auto height = jpeg_cfg.height;
    
    std::cout << "初始化JPEG编码器...\n";
    auto t_init_start = steady_clock::now();
    auto jpeg_encoder = std::make_unique<JpegEncoder>(jpeg_cfg);
    auto t_init_end = steady_clock::now();
    std::cout << "编码器初始化耗时: " 
              << duration_cast<milliseconds>(t_init_end - t_init_start).count() 
              << " ms\n";
    
    // 配置摄像头
    CameraController::Config cctrCfg{
        .buffer_count = 2,
        .plane_count = 2,
        .use_dmabuf = true,
        .device = "/dev/video0",
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .format = V4L2_PIX_FMT_NV12
    };
    
    auto cctr = cameraSetUp(cctrCfg);
    if (!cctr){
        std::cerr << "CameraController 初始化失败\n"; 
        return -1;
    }

    // 启动摄像头采集
    cctr->start();
    cctr->setThreadAffinity(2);

    std::atomic<int> cpc{5};
    std::cout << "\n=== 开始拍摄性能测试 ===\n";
    std::cout << "分辨率: " << width << "x" << height << "\n";
    std::cout << "质量:   " << jpeg_cfg.quality << "/10\n";
    std::cout << "帧数:   "<<cpc.load()<<" 帧\n\n";
    
    auto test_start = steady_clock::now();
    while(cpc > 0) {
        auto loop_start = steady_clock::now();
        
        // ============ 测试点 1: 帧出队 ============
        auto t1 = steady_clock::now();
        FramePtr frame;
        if(!rawFrameQueue->try_dequeue(frame)) continue;
        auto t2 = steady_clock::now();
        perf.frame_dequeue_us.push_back(duration_cast<microseconds>(t2 - t1).count());
        
        // ============ 测试点 2: JPEG编码 ============
        auto slotDma = frame->sharedState(0)->dmabuf_ptr;
        if (!slotDma) {
            std::cerr << "[ERROR] DmaBuffer 无效\n";
            return -1;
        }
        
        auto t3 = steady_clock::now();
        bool success = jpeg_encoder->captureFromDmabuf(slotDma);
        auto t4 = steady_clock::now();
        
        if (!success) {
            std::cerr << "[ERROR] JPEG编码失败\n";
            return -1;
        }
        
        auto encode_us = duration_cast<microseconds>(t4 - t3).count();
        perf.jpeg_encode_us.push_back(encode_us);
        
        // ============ 端到端延迟 ============
        auto loop_end = steady_clock::now();
        auto e2e_us = duration_cast<microseconds>(loop_end - loop_start).count();
        perf.end_to_end_us.push_back(e2e_us);
        
        // 实时反馈
        int frame_idx = 5 - cpc.load();
        std::cout << "[帧 " << frame_idx << "] "
                  << "出队: " << std::setw(6) << perf.frame_dequeue_us.back() << " μs, "
                  << "编码: " << std::setw(8) << encode_us << " μs, "
                  << "总计: " << std::setw(8) << e2e_us << " μs\n";
        
        perf.frame_count++;
        cpc.fetch_sub(1);
    }
    
    auto test_end = steady_clock::now();
    perf.total_time_ms = duration_cast<milliseconds>(test_end - test_start).count();
    
    cctr->stop();
    
    // ============ 打印详细报告 ============
    perf.print();
    
    // ============ 额外诊断信息 ============
    std::cout << "\n========== 系统诊断 ==========\n";
    
    // 检查文件大小
    system("ls -lh /tmp/photos/*.jpg 2>/dev/null | tail -5");
    
    // VPU 状态(如果可访问)
    std::cout << "\nVPU 状态:\n";
    system("cat /sys/kernel/debug/mpp_service/session 2>/dev/null || echo '  (需要root权限)'");
    
    std::cout << "\nVPU 时钟频率:\n";
    system("cat /sys/kernel/debug/clk/clk_summary 2>/dev/null | grep vpu || echo '  (需要root权限)'");
    
    return 0;
}

/*
int cameraRecordTest(){
    static std::atomic_bool running{true};
    auto handleSignal = [](int signal) {
        if (signal == SIGINT) {
            std::cout << "Ctrl+C received, stopping..." << std::endl;
            running.store(false);
        }
    };
    std::signal(SIGINT, handleSignal);

    // 创建队列
    std::shared_ptr<FrameQueue> rawFrameQueue  	= std::make_shared<FrameQueue>(2);
    // 配置初始化
    auto cfg = DefaultConfigs::defconfig_1080p_video();
    // cfg.rc_fps_in_num=20;
    // cfg.rc_fps_out_num=20;
    // cfg.rc_gop = 20 * 2;
    auto width = cfg.prep_width;
    auto height = cfg.prep_height;
    CameraController::Config cctrCfg{
        .buffer_count = 15,
        .plane_count = 2,
        .use_dmabuf = true,
        .device = "/dev/video0",
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .format = V4L2_PIX_FMT_NV12
    };
    // 实例化
    std::shared_ptr<CameraController> cctr = std::make_shared<CameraController>(cctrCfg);// 摄像头控制器 
    MppEncoderCore core(cfg, 0);                        // 编码核心
    StreamWriter writer("camera_record_720p_nv12.h264");// 流式写入器
    // 摄像头回调
    cctr->setFrameCallback([&](FramePtr f) { 
        rawFrameQueue->enqueue(std::move(f));
    });
    std::cout << "=== 摄像头录制测试开始 ===\n";
    // 启动摄像头采集
    cctr->start();
    cctr->setThreadAffinity(2); // 绑定核心2
    // 录像1分钟
    auto startTime = std::chrono::steady_clock::now();
    while (running) {
        if (!pool.isreadly()){
            continue;
        }

        // 获取NV12帧
        FramePtr frame;
        if (!rawFrameQueue->try_dequeue(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (!frame) {
            std::cerr << "获取帧失败！\n";
            continue;
        }
        // 获取 DMA buffer
        auto dmaSrc = frame->sharedState(0)->dmabuf_ptr;
        if (!dmaSrc) {
            std::cerr << "获取 DMA buffer 失败！\n";
            continue;
        }
        
    }
    // pool.enqueu(dmaSrc);
    // std::cout << "." << std::flush;
    // auto currentTime = std::chrono::steady_clock::now();
    // auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime).count();
    // if (elapsed >= 30) {
    //     running = false;
    //     break;
    // }
    
    cctr->stop();
    std::cout << "\n搞定！播放命令：\n";
    std::cout << "ffplay camera_record_720p_nv12.h264\n";   
    return 0;    
}
*/
} // ENCTest