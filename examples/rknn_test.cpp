/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-02 17:57:24
 * @FilePath: /EdgeVision/examples/rknn_test.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
/*
 * YOLOv5 RKNN 推理示例
 */
#include <iostream>
#include <csignal>

#include "yolov5s.h"
#include "rknnPool.h"
#include "drm/deviceController.h"
#include "progressBar.h"

static std::atomic_bool running{true};
static void handleSignal(int signal) {
    if (signal == SIGINT) {
        std::cout << "Ctrl+C received, stopping..." << std::endl;
        running.store(false);
    }
}

// 问题根源
// OpenCV 的 cv::Mat 是紧密排列的数据（无填充），但 DmaBuffer 会按硬件要求对齐内存（每行有 padding）。直接 memcpy 整块数据导致内存布局不匹配，造成了 45 度倾斜和图像错位。
// 解决方案
// 在 readImage 函数中改为逐行拷贝，使用 dma_buf->pitch() 获取正确的行步长，这样数据就按照 DmaBuffer 期望的对齐方式存储了。
// 在 RGA 处理时，使用 pitch / 3 计算出正确的像素 stride（490），RGA 就能正确读取和缩放图像了。

void simpleTest(const std::vector<DmaBufferPtr>&Images, const std::string& model_path){
    Yolov5s yolo(model_path, "./coco_80_labels_list.txt");
    yolo.init(yolo.getCurrentContext(), false);
    int img_idx = 0;
    for (auto& img : Images){    
        fprintf(stdout, "\n========== Processing Image %d ==========\n", img_idx++);

        auto output = yolo.infer(img, true);
        // 8. 绘制
        std::string postpath = "./detected_result_" + std::to_string(img_idx) + ".jpg";
        saveImage(postpath, output);
        
        fprintf(stdout, "========== Image %d Done ==========\n\n", img_idx - 1);
    }
};

void mutiTest(const std::vector<DmaBufferPtr>& Images, const std::string& model_path){
    rknnPool<Yolov5s, DmaBufferPtr, DmaBufferPtr> pool(model_path, "./coco_80_labels_list.txt", 3);
    pool.init();
    
    int img_idx = 0;
    
    for (auto& img : Images){    
        fprintf(stdout, "\n========== Processing Image %d ==========\n", img_idx++);
        
        // 开始计时
        auto start = std::chrono::high_resolution_clock::now();
        if (pool.put(img) < 0){
            fprintf(stderr, "Failed to put image to rknnpool.\n");
            continue;
        }
        DmaBufferPtr output;
        if (pool.get(output, 0) < 0){
            fprintf(stderr, "Failed to get output image from rknnpool.\n");
            continue;
        }
        
        // 结束计时并输出
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        fprintf(stdout, "Inference time: %lld ms\n", duration.count());
        
        // 保存结果
        std::string postpath = "./detected_result_" + std::to_string(img_idx) + ".jpg";
        saveImage(postpath, output);
        
        fprintf(stdout, "========== Image %d Done ==========\n\n", img_idx - 1);
    }
};

void imageTest(int mode){
    if (mode > 2 || mode < 1) return;
    rknn_app_context app_context{};
    std::string model_path = "./yolov5s_relu.rknn";
    std::string image0_path = "./bus.jpg";
    std::string image1_path = "./image.png";
    std::vector<DmaBufferPtr> Images;

    auto image0_buf = std::move(readImage(image0_path));
    auto image1_buf = std::move(readImage(image1_path));
    if (nullptr == image0_buf || nullptr == image1_buf) {
        std::cerr << "Failed to read image\n";
        return;
    }
    Images.emplace_back(std::move(image0_buf));
    Images.emplace_back(std::move(image1_buf));

    switch (mode)
    {
    case 1:
        simpleTest(Images, model_path);
        break;
    case 2:
        mutiTest(Images, model_path);
        break;
    default:
        break;
    }
}

void videoTest(){
    auto read2dma = [](const cv::Mat& image){
        // 创建 DMABUF
        DmaBufferPtr dma_buf = nullptr;
        dma_buf = DmaBuffer::create(image.cols, image.rows, DRM_FORMAT_RGB888, 0);
        if (!dma_buf) {
            std::cerr << "Failed to create DmaBuffer" << std::endl;
            return dma_buf;
        }
        
        uint8_t* dst = (uint8_t*)dma_buf->map();
        uint8_t* src = image.data;
        
        int height = image.rows;
        int src_row_bytes = image.cols * 3; // OpenCV 行字节数（紧密）
        int dst_stride = dma_buf->pitch();  // DmaBuffer 行字节数（对齐）

        for (int y = 0; y < height; y++) {  // 逐行拷贝
            memcpy(dst + y * dst_stride, src + y * src_row_bytes, src_row_bytes);
        }
        dma_buf->unmap();
        return dma_buf;
    };
    cv::VideoCapture cap("BiliBili.mp4");
    if (!cap.isOpened()) {
        std::cerr << "Failed to read video.\n";
        return;
    }

    std::string model_path = "./yolov5s_relu.rknn";
    rknnPool<Yolov5s, DmaBufferPtr, DmaBufferPtr> pool(model_path, "./coco_80_labels_list.txt", 2);
    pool.init();
    
    // 获取视频参数
    int fps = cap.get(cv::CAP_PROP_FPS);
    int width = cap.get(cv::CAP_PROP_FRAME_WIDTH);
    int height = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    int total_frames = cap.get(cv::CAP_PROP_FRAME_COUNT);
    
    // VideoWriter 创建
    int fourcc = cv::VideoWriter::fourcc('m','p','4','v');  // MPEG-4 软件编码
    cv::VideoWriter writer("output_result.mp4", fourcc, fps, cv::Size(width, height));
    if (!writer.isOpened()) {
        std::cerr << "Failed to open VideoWriter\n";
        return;
    }

    // 创建进度条管理器
    ProgressManager progress;
    int readBar = progress.addBar("Reading frames", total_frames);
    int procBar = progress.addBar("Processing frames", total_frames);

    std::atomic<int> processing{0};
    const int MAX_BACKLOG = 20; // 固定最大积压帧数

    // ========== 读取线程 ==========
    std::thread readThread([&]() {
        cv::Mat image;
        int frame_count = 0;
        while (running) {
            int backlog = frame_count - processing.load(std::memory_order_relaxed);
            if (backlog >= MAX_BACKLOG) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            cap >> image;
            if (image.empty()) {
                if (frame_count != total_frames) continue;
                std::cout << "Video reading finished. Total frames: " << frame_count << std::endl;
                break;
            } 
            
            auto dmabuf = read2dma(image);
            if (nullptr == dmabuf) continue;

            while (pool.put(dmabuf) == 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            ++frame_count;
            progress.update(readBar, frame_count);
        }
        progress.done(readBar);
    });
    

    int processed_count = 0;
    int empty_count = 0;
    // 尝试获取处理结果
    DmaBufferPtr output = nullptr;
    while (running) {
        auto ret = pool.get(output, 0);
        processing.fetch_add(1);
        if (ret == 1) {
            fprintf(stderr, "\rNo data."); // 使用回车符避免换行
            if (empty_count < 6) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                empty_count++;
                continue;
            }
            running.store(false);
            break;
        }
        if (!output) continue;
        
        // 将处理结果转回 OpenCV Mat 并写入文件
        cv::Mat result_image = mapDmaBufferToMat(output, true);
        if (!result_image.empty()) {
            cv::cvtColor(result_image, result_image, cv::COLOR_RGB2BGR);
            writer.write(result_image);
            output->unmap();
            result_image.release();
            ++processed_count;
            progress.update(procBar, processed_count);
        }
        if (processed_count >= total_frames) break;
    }

    progress.done(procBar);
    running.store(false);
    if (readThread.joinable()) readThread.join();
    writer.release();
    cap.release();

    std::cout << "\n\n=== Processing Completed ===\n";
    std::cout << "Input: " << width << "x" << height << ", " << fps << " FPS, " << total_frames << " frames\n";
    std::cout << "Output saved as output_result.mp4 (" << processed_count << " frames)\n";
    std::cout << "=============================\n";
}

int main(int argc, char const *argv[])
{
    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " [--imgtest | --videotest]" << std::endl;
        return 1;
    }
    std::signal(SIGINT, handleSignal);
    std::unordered_map<std::string, std::function<void()>> testMap = {
        {"--imgtest", [&]() {
            if (argc < 3) {
                std::cerr << "错误: --imgtest 需要指定模式参数" << std::endl;
                return;
            }
            int mode = std::atoi(argv[2]);  // 使用atoi转换字符串为int
            imageTest(mode);
        }},
        {"--videotest", videoTest }
    };
    
    std::string inputArg = argv[1];
    
    // 在map中查找输入的参数
    auto it = testMap.find(inputArg);
    if (it == testMap.end()) {
        std::cerr << "未知选项: " << inputArg << std::endl;
        std::cerr << "可用选项: --imgtest, --videotest" << std::endl;
        return 1;
    } 
    
    int ret = 0;
    DrmDev::fd_ptr = DeviceController::create();
    
    // 找到了对应的测试函数
    try {
        it->second(); // 执行找到的函数
    } catch (const std::exception& e) {
        std::cerr << "运行时错误: " << e.what() << std::endl;
        ret = 1;
    } catch (...) {
        std::cerr << "未知错误发生" << std::endl;
        ret = 1;
    }
    
    return ret;  // 返回实际的ret值
}