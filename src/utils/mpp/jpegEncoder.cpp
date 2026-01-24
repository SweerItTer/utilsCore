// mpp/jpegEncoder.cpp
#include "mpp/jpegEncoder.h"
#include "mpp/mppResourceGuard.h"
#include "mpp/formatTool.h"
#include <iomanip>
#include <sstream>
#include <sys/stat.h> // POSIX 文件操作

JpegEncoder::JpegEncoder(const Config& cfg)
    : initialized_(false), config_(cfg) {
    encoder_ctx_ = std::make_unique<MppEncoderContext>(config_.toMppConfig());

    if (!encoder_ctx_->ctx() || !encoder_ctx_->api()) {
        fprintf(stderr, "[JpegEncoder] Initialization failed!\n");
        throw std::runtime_error("[JpegEncoder] Initialization failed!");
    }

    initialized_.store(true);
    ctx = encoder_ctx_->ctx();
    mpi = encoder_ctx_->api();

    mkdir(config_.save_dir.c_str(), 0755);
    fprintf(stdout, "[JpegEncoder] Initialized: %ux%u, quality=%d\n",
            config_.width, config_.height, config_.quality);
}

bool JpegEncoder::resetConfig(const Config& cfg) {
    config_ = cfg;
    return encoder_ctx_->resetConfig(config_.toMppConfig());
}

bool JpegEncoder::captureFromDmabuf(const DmaBufferPtr dmabuf) {
    if (!initialized_ || !dmabuf) {
        fprintf(stderr, "[JpegEncoder] Not initialized or invalid dmabuf\n");
        return false;
    }

    MppFrame frame = nullptr;
    MppBuffer buffer = nullptr;
    
    MppFrameGuard   frame_guard(&frame);

    // 导入 dmabuf
    MppBufferInfo import_info{};
    import_info.type = MPP_BUFFER_TYPE_EXT_DMA;
    import_info.fd   = dmabuf->fd();
    import_info.size = dmabuf->size();

    if (MPP_OK != mpp_buffer_import(&buffer, &import_info) || !buffer) {
        fprintf(stderr, "[JpegEncoder] mpp_buffer_import failed\n");
        return false;
    }
    MppBufferGuard  buffer_guard(buffer);

    // 创建 frame
    if (MPP_OK != mpp_frame_init(&frame) || !frame) {
        fprintf(stderr, "[JpegEncoder] mpp_frame_init failed\n");
        if (buffer) mpp_buffer_put(buffer);
        return false;
    }

    // 设置 frame 参数
    mpp_frame_set_width(frame, dmabuf->width());
    mpp_frame_set_height(frame, dmabuf->height());
    mpp_frame_set_hor_stride(frame, dmabuf->pitch());
    mpp_frame_set_ver_stride(frame, dmabuf->height());
    mpp_frame_set_fmt(frame, convertDrmToMppFormat(dmabuf->format()));
    mpp_frame_set_buffer(frame, buffer);

    // 生成文件名
    std::string filepath = generateFilename();

    // 编码并保存
    if (!encodeToFile(frame, filepath)) {
        fprintf(stderr, "[JpegEncoder] Failed to save image to: %s\n", filepath.c_str());
        return false;
    }

    fprintf(stdout, "[JpegEncoder] Saved to: %s\n", filepath.c_str());
    return true;
}

bool JpegEncoder::encodeToFile(MppFrame frame, const std::string& filepath) {
    // 提交编码
    if (MPP_OK != mpi->encode_put_frame(ctx, frame)) {
        fprintf(stderr, "[JpegEncoder] encode_put_frame failed\n");
        return false;
    }

    // 获取编码结果
    MppPacket packet = nullptr;
    const int max_retry = 50;
    
    for (int i = 0; i < max_retry; ++i) {
        RK_S32 ret = mpi->encode_get_packet(ctx, &packet);
        if (ret == MPP_OK && packet) {
            break;
        }
        if (ret != MPP_ERR_TIMEOUT) {
            fprintf(stderr, "[JpegEncoder] encode_get_packet error: %d\n", ret);
            return false;
        }
        usleep(2000);  // 2ms
    }

    if (!packet) {
        fprintf(stderr, "[JpegEncoder] encode timeout\n");
        return false;
    }

    // 写入文件
    void* data = mpp_packet_get_data(packet);
    size_t len = mpp_packet_get_length(packet);

    FILE* fp = fopen(filepath.c_str(), "wb");
    if (!fp) {
        fprintf(stderr, "[JpegEncoder] fopen failed: %s\n", filepath.c_str());
        mpp_packet_deinit(&packet);
        return false;
    }

    size_t written = fwrite(data, 1, len, fp);
    fclose(fp);
    mpp_packet_deinit(&packet);

    return (written == len);
}

std::string JpegEncoder::generateFilename() {
    // 生成基于时间戳的文件名
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;
    
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&t);

    std::ostringstream oss;
    oss << config_.save_dir << "/"
        << std::put_time(&tm, "%Y%m%d_%H%M%S")
        << "_" << std::setfill('0') << std::setw(3) << ms.count()
        << ".jpg";

    return oss.str();
}