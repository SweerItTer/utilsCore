/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-11-17 20:29:28
 * @FilePath: /EdgeVision/examples/encoderTest.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "mpp/encoderCore.h"
#include "mpp/streamWriter.h"
#include "rga/rgaConverter.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

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
    auto width = cfg.prep_width;
    auto height = cfg.prep_height;
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
    RgaConverter& converter = RgaConverter::instance(); // RGA 单例
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
        
        // 获取空闲 slot
        auto tmp = core.acquireWritableSlot();
        auto slotDma = tmp.first;
        auto slot_id = tmp.second;
        if (!slotDma || slot_id < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            std::cerr << "获取可用 slot 失败！\n";
            continue;
        }
        // 使用 SlotGuard 确保 slot 释放
        SlotGuard guard(&core, slot_id);
        // RGA 拷贝 // 详细问题见 注意.txt
        rga_buffer_t src = wrapbuffer_fd(dmaSrc->fd(), dmaSrc->width(), dmaSrc->height(), formatDRMtoRGA(dmaSrc->format()));
        src.wstride = dmaSrc->pitch();
        src.hstride = dmaSrc->height();
        im_rect srcR{0,0,static_cast<int>(dmaSrc->width()), static_cast<int>(dmaSrc->height())};

        rga_buffer_t dst = wrapbuffer_fd(slotDma->fd(), slotDma->width(), slotDma->height(), formatDRMtoRGA(slotDma->format()));
        dst.wstride = slotDma->pitch();
        dst.hstride = slotDma->height();
        im_rect dstR{0,0,static_cast<int>(slotDma->pitch()), static_cast<int>(slotDma->height())};
        RgaConverter::RgaParams param{
            .src = src,
            .src_rect=srcR,
            .dst=dst,
            .dst_rect=dstR
        };
        if(converter.ImageResize(param) != IM_STATUS_SUCCESS){
            std::cerr << "RGA-copy 失败！\n";
            return -1;
        }
        // 提交编码
        auto meta = core.submitFilledSlot(slot_id);
        // 释放所有权交由 StreamWriter
        guard.release(); 
        writer.pushMeta(meta);
        std::cout << "." << std::flush;
    }
    core.endOfthisEncode();
    writer.stop();
    cctr->stop();
    std::cout << "\n搞定！播放命令：\n";
    std::cout << "ffplay camera_record_720p_nv12.h264\n";   
    return 0;    
}

} // ENCTest