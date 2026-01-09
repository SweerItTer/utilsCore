/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-11-17 18:59:39
 * @FilePath: /EdgeVision/include/utils/mpp/encoderContext.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include <rockchip/rk_type.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/rk_venc_rc.h>
#include <rockchip/rk_venc_cmd.h>

/**
 * @brief MppEncoderContext
 * 
 * 负责 Rockchip MPP 编码器的完整上下文管理, 包括:
 * - MppCtx/MppApi/MppEncCfg 的 RAII 生命周期管理
 * - 编码器的初始化、配置、重配置
 * - 支持动态修改编码参数 (分辨率, 码率, FPS, 编码格式等)
 * 
 * 使用者只需创建 MppEncoderContext 并向 MPP 提交帧, 编码流程由外部驱动.
 */
class MppEncoderContext {
public:
    /**
     * @brief 编码器类型定义
     */
    enum class CodingType {
        H264 = MPP_VIDEO_CodingAVC,   /**< H.264 / AVC */
        H265 = MPP_VIDEO_CodingHEVC,  /**< H.265 / HEVC */
        VP8  = MPP_VIDEO_CodingVP8,   /**< VP8 */
        MJPEG= MPP_VIDEO_CodingMJPEG  /**< MJPEG */
    };

    /**
     * @brief 编码器配置结构体, 用于初始化和动态重置编码器参数
     * 
     * 所有字段均为用户可调参数, 部分字段若为 0 或 -1 则表示使用 MPP 默认策略.
     */
    struct Config {
        /** 基础编码参数 */
        CodingType codec_type = CodingType::H264; /**< 编码格式 */
        int prep_width = 1920;                   /**< 输入图像宽度 */
        int prep_height = 1080;                  /**< 输入图像高度 */
        int rc_fps_out_num = 30;                 /**< 输出帧率 */
        int rc_bps_target = 4 * 1024 * 1024;     /**< 目标码率(bps) */
        int rc_gop = 60;                         /**< GOP 长度 */

        /** 预处理参数 (stride/rotation/mirror) */
        int prep_hor_stride = 0;                 /**< 水平 stride, 0 为自动对齐 */
        int prep_ver_stride = 0;                 /**< 垂直 stride */
        MppFrameFormat prep_format = MPP_FMT_YUV420SP; /**< 输入像素格式, 默认 NV12 */
        int prep_rotation = 0;                   /**< 旋转角度, 支持 0/90/180/270 */
        bool prep_mirroring = false;             /**< 镜像 */

        /** 码率控制参数 */
        int rc_mode = MPP_ENC_RC_MODE_VBR;       /**< CBR/VBR/FIXQP */
        int rc_bps_min = 0;                      /**< 最小码率, 0 为自动 */
        int rc_bps_max = 0;                      /**< 最大码率 */
        int rc_fps_in_num = 30;                  /**< 输入帧率 (参考MPP测试代码默认30) */
        int rc_fps_in_denorm = 1;                /**< 输入帧率分母 */
        int rc_fps_out_denorm = 1;               /**< 输出帧率分母 */
        int rc_fps_in_flex = 0;                  /**< 输入帧率自适应 */
        int rc_fps_out_flex = 0;                 /**< 输出帧率自适应 */
        int rc_qp_init = -1;                     /**< 初始 QP, 仅 FIXQP 模式有效 */
        int rc_qp_min_i  = 0;                    /**< I 帧最小 QP */
        int rc_qp_max_i  = 0;                    /**< I 帧最大 QP */
        int rc_qp_min    = 0;                    /**< P 帧最小 QP */
        int rc_qp_max    = 0;                    /**< P 帧最大 QP */
        int rc_qp_ip     = 2;                    /**< I/P帧QP差值 (参考MPP测试代码默认2) */
        

        /** H.264/H.265 编码配置 */
        int h264_profile = 77;                   /**< H.264 profile, 默认 baseline */
        int h264_level   = 30;                   /**< H.264 level */
        int hevc_profile = 1;                    /**< H.265 主 profile */
        int hevc_level   = 30;                   /**< H.265 level */

        /** JPEG 编码相关 */
        int jpeg_q_factor = 90;                  /**< JPEG 默认质量因子, 推荐范围 70–95 */
        int jpeg_qf_min   = 1;                   /**< JPEG 最低质量因子 */
        int jpeg_qf_max   = 99;                  /**< JPEG 最高质量因子 */

        /** SEI / header 输出控制 */
        int sei_mode = MPP_ENC_SEI_MODE_ONE_FRAME; /**< SEI 插入模式 */
        int header_mode = MPP_ENC_HEADER_MODE_EACH_IDR; /**< SPS/PPS 输出策略 */

        /** 高级参数 */
        int split_mode = 0;                      /**< Slice 分割模式 */
        int split_arg  = 0;                      /**< Slice 参数 */
        int split_out  = 0;                      /**< Slice 输出 */

        int roi_enable = 0;                      /**< 启用 ROI */
        int osd_enable = 0;                      /**< 启用 OSD */
        int osd_mode   = MPP_ENC_OSD_PLT_TYPE_DEFAULT; /**< OSD 模式 */

        int user_data_enable = 0;                /**< 是否写入 user-data SEI */

        /** I 帧强制 / IDR 控制 */
        int rc_force_idr_interval = 0;           /**< 0 = 不强制, >0 = 每 N 帧强制生成 I 帧 */

        /** 低延时模式 */
        bool rc_low_delay = false;               /**< 是否开启低延时模式, 对 GOP 与 I 帧行为有影响 */

        /** 最大重编码次数 */
        int rc_max_reenc_times = 1;              /**< 单帧最大重编码次数, 低延时模式下可设置为0 */

        /** 超大帧处理 */
        int rc_super_mode = 0;                   /**< 超大帧处理策略: 0=无策略, 1=丢弃, 2=重编码 */
        int rc_super_i_thd = 0;                  /**< 超大 I 帧阈值, 字节数 */
        int rc_super_p_thd = 0;                  /**< 超大 P 帧阈值, 字节数 */

        /** IP 比例控制 */
        int rc_max_i_prop = 30;                  /**< 最大 I/P 比例, 用于钳位 I 帧比特数 */
        int rc_min_i_prop = 10;                  /**< 最小 I/P 比例 */
        int rc_init_ip_ratio = 160;              /**< 初始 I/P 比例 */

        /** QP 分层 (可用于 H.264/H.265) */
        bool hier_qp_enable = false;             /**< 是否启用 QP 分层 */
        int hier_frame_num[4] = {0};             /**< 每层帧数, 层数最多 4 */
        int hier_qp_delta[4] = {0};              /**< 每层 QP 相对第0层 P 帧的偏移 */

        /** 去呼吸效应 */
        bool rc_debreath_en = false;             /**< 是否开启去呼吸效应 */
        int rc_debreath_strength = 0;            /**< 去呼吸强度, [0,35], 值越大改善越弱 */

        /** 丢帧控制 */
        int rc_drop_mode = 0;                    /**< 0=关闭, 1=正常丢帧, 2=pskip构造 */
        int rc_drop_thd = 20;                    /**< 丢帧阈值, 公式: bps_max*(1+drop_thd/100) */
        int rc_drop_gap = 1;                     /**< 最大允许连续丢帧数 */

        /** 可选统计控制 */
        int rc_stats_time = 3;                   /**< 瞬时码率统计时间, 单位秒 */

        /** H.264/H.265 CABAC 与约束 */
        bool cabac_enable = false;               /**< 是否启用 CABAC */
        int cabac_idc = 0;                       /**< CABAC 初始化 ID, 0~2 */
        int rc_poc_type = 0;                     /**< pic_order_cnt_type */
        int rc_log2_max_poc_lsb = 0;             /**< log2_max_pic_order_cnt_lsb_minus4, 仅 poc_type=0 有效 */
        int rc_log2_max_frm_num = 0;             /**< log2_max_frame_num_minus4 */

        /** 可选 slice 控制 */
        bool rc_prefix_mode = false;             /**< 是否在 SEI 前插入 prefix nal */
        int rc_base_layer_pid = 0;               /**< 基准层优先级ID */

        /** 色彩范围扩展 */
        int rc_color_range_override = -1;        /**< -1: 默认, 0: 未指定, 1: MPEG, 2: JPEG */

        /** 未来扩展保留 */
        void* user_ext = nullptr;                /**< 用户自定义扩展指针 */
    };

public:
    /**
     * @brief 构造函数, 使用给定配置初始化 MPP 编码器上下文
     * 
     * @param cfg 用户定义的编码配置
     */
    explicit MppEncoderContext(const Config &cfg);

    /**
     * @brief 析构函数, 自动释放 MPP 上下文与资源
     */
    ~MppEncoderContext();

    /**
     * @brief 使用新的配置重置编码器
     * 
     * 会触发 MPP 内部重新配置编码参数, 但不会销毁 ctx
     * 
     * @param cfg 新配置
     * @return true 重置成功
     * @return false 重置失败
     */
    bool resetConfig(const Config &cfg);

    /** 获取 MPP 上下文指针 */
    MppCtx ctx() const { return mCtx; }

    /** 获取 MPP API 接口 */
    MppApi* api() const { return mpi; }

    /** 获取 MPP 编码配置句柄 */
    MppEncCfg encCfg() const { return mCfg; }

    /** 获取当前已生效的配置 */
    const Config* getmCfg() const { return &mCurCfg; }

    /**
     * @brief 验证配置是否适合ffmpeg转换
     *
     * 检查关键参数以确保生成的视频流可以被ffmpeg正确转换为MP4
     *
     * @param cfg 要验证的配置
     * @return true 配置兼容ffmpeg
     * @return false 配置可能导致ffmpeg转换问题
     */
    static void validateForFfmpeg(const Config& cfg) {
        // Check format compatibility
        if (cfg.prep_format != MPP_FMT_YUV420SP && cfg.prep_format != MPP_FMT_YUV420P) {
            fprintf(stderr, "[MppEncoderContext] WARNING: Format %d may not be optimal for ffmpeg. Use MPP_FMT_YUV420SP (NV12).\n", cfg.prep_format);
        }

        // Check color range
        if (cfg.rc_color_range_override != 1) {
            fprintf(stderr, "[MppEncoderContext] WARNING: Color range %d may cause issues. Set to 1 (MPEG range) for ffmpeg.\n", cfg.rc_color_range_override);
        }

        // Check codec type
        if (cfg.codec_type != CodingType::H264 && cfg.codec_type != CodingType::H265) {
            fprintf(stderr, "[MppEncoderContext] WARNING: Codec type %d may not be optimal for MP4. Use H264 or H265.\n", static_cast<int>(cfg.codec_type));
        }

        // Check profile for H264
        if (cfg.codec_type == CodingType::H264 && cfg.h264_profile != 77 && cfg.h264_profile != 100) {
            fprintf(stderr, "[MppEncoderContext] WARNING: H264 profile %d may not be widely supported. Use 77 (Main) or 100 (High).\n", cfg.h264_profile);
        }
    }

    /**
     * @brief 自动修复配置以确保ffmpeg兼容性
     *
     * 自动设置关键参数以避免ffmpeg转换时的颜色问题
     *
     * @param cfg 要修复的配置引用
     */
    static void fixForFfmpeg(Config& cfg) {
        // Force NV12 format for best ffmpeg compatibility
        cfg.prep_format = MPP_FMT_YUV420SP;

        // Force MPEG color range (16-235)
        cfg.rc_color_range_override = 1;

        // Ensure H264/H265 for MP4 compatibility
        if (cfg.codec_type != CodingType::H264 && cfg.codec_type != CodingType::H265) {
            cfg.codec_type = CodingType::H264;
        }

        // Set appropriate profile for H264
        if (cfg.codec_type == CodingType::H264 && cfg.h264_profile != 77 && cfg.h264_profile != 100) {
            cfg.h264_profile = 77; // Main Profile
        }

        // Set appropriate level based on resolution
        if (cfg.codec_type == CodingType::H264) {
            int pixels = cfg.prep_width * cfg.prep_height;
            if (pixels <= 1280 * 720) {
                cfg.h264_level = 31; // Level 3.1 for 720p
            } else if (pixels <= 1920 * 1080) {
                cfg.h264_level = 40; // Level 4.0 for 1080p
            } else {
                cfg.h264_level = 51; // Level 5.1 for 4K
            }
        }
    }

private:
    /**
     * @brief 初始化 MPP 上下文, 创建 ctx/api/cfg
     * @return true 初始化成功
     * @return false 初始化失败
     */
    bool init();

    /**
     * @brief 把用户 Config 应用到 MPP
     * 
     * 包括 stride/codec/bitrate/fps/gop 等所有参数
     */
    bool applyConfig();

private:
    MppCtx mCtx = nullptr;      /**< MPP 上下文对象 */
    MppApi* mpi = nullptr;      /**< MPP API 接口 */
    MppEncCfg mCfg = nullptr;   /**< 编码配置句柄 */
    Config mCurCfg;             /**< 当前生效的配置 */
};

namespace DefaultConfigs {
/// -------------------- 预设配置 -------------------------
/// 获取简单视频录制配置 (基于MPP测试代码官方默认值)
inline static MppEncoderContext::Config defconfig_video_recording(int width, int height, int fps = 30, int bitrate_mbps = 4) {
    MppEncoderContext::Config cfg;

    // 基础参数 (参考MPP测试代码第297-304行默认值)
    cfg.prep_width   = width;
    cfg.prep_height  = height;
    cfg.rc_fps_in_num     = fps;        // 输入帧率 (参考第300行: 默认30)
    cfg.rc_fps_out_num    = fps;        // 输出帧率 (参考第304行: 默认30)
    cfg.rc_fps_in_denorm  = 1;          // 输入帧率分母 (参考第298行: 默认1)
    cfg.rc_fps_out_denorm = 1;          // 输出帧率分母 (参考第302行: 默认1)
    cfg.rc_fps_in_flex    = 0;          // 输入帧率自适应 (参考第318行)
    cfg.rc_fps_out_flex   = 0;          // 输出帧率自适应 (参考第321行)

    // 码率计算 (参考MPP测试代码第306-307行: width * height / 8 * fps)
    if (bitrate_mbps > 0) {
        cfg.rc_bps_target = bitrate_mbps * 1024 * 1024;
    } else {
        // 使用MPP官方默认码率计算方式
        cfg.rc_bps_target = width * height / 8 * (fps / 1);
    }

    // 编码格式 (参考MPP测试代码默认使用H264)
    cfg.codec_type   = MppEncoderContext::CodingType::H264;
    cfg.prep_format  = MPP_FMT_YUV420SP;  // NV12 format for ffmpeg compatibility

    // 码率控制 (参考MPP测试代码第342-347行: VBR模式)
    cfg.rc_mode = MPP_ENC_RC_MODE_VBR;
    // VBR模式边界 (参考第345-346行)
    cfg.rc_bps_max = cfg.rc_bps_target * 17 / 16;  // +6.25%
    cfg.rc_bps_min = cfg.rc_bps_target * 1 / 16;   // -93.75%

    // GOP设置 (参考MPP测试代码第324行: fps * 2)
    cfg.rc_gop = fps * 2;  // 2-second GOP
    cfg.rc_force_idr_interval = 1;  // 强制生成I帧

    // QP设置 (参考MPP测试代码第373-378行)
    cfg.rc_qp_init = -1;   // 使用编码器自动QP (参考第373行)
    cfg.rc_qp_min  = 10;   // 最小QP (参考第375行)
    cfg.rc_qp_max  = 51;   // 最大QP (参考第375行)
    cfg.rc_qp_min_i = 10;  // I帧最小QP (参考第377行)
    cfg.rc_qp_max_i = 51;  // I帧最大QP (参考第377行)
    // QP IP差值 (参考第378行: 2)
    // 注意: 这个参数在您的Config结构体中缺失, 需要手动设置

    // H.264配置 (参考MPP测试代码第414-426行)
    cfg.h264_profile = 100;  // High Profile (参考第414行)
    cfg.h264_level   = 40;   // Level 4.0 (参考第423行)
    cfg.cabac_enable = true; // CABAC编码 (参考第424行)
    cfg.cabac_idc    = 0;    // CABAC初始化ID (参考第425行)

    // 根据分辨率自动调整level (参考MPP测试代码注释)
    int pixels = width * height;
    if (pixels <= 1280 * 720) {
        cfg.h264_level = 31; // Level 3.1 for 720p
    } else if (pixels <= 1920 * 1080) {
        cfg.h264_level = 40; // Level 4.0 for 1080p
    } else {
        cfg.h264_level = 51; // Level 5.1 for 4K
    }

    // 丢帧控制 (参考MPP测试代码第327-329行)
    cfg.rc_drop_mode = MPP_ENC_RC_DROP_FRM_DISABLED; // 禁用丢帧
    cfg.rc_drop_thd  = 20;                           // 丢帧阈值20%
    cfg.rc_drop_gap  = 1;                            // 不连续丢帧

    // SEI和header设置 (参考MPP测试代码第460-474行)
    cfg.sei_mode    = MPP_ENC_SEI_MODE_ONE_FRAME;    // SEI插入模式
    cfg.header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;  // SPS/PPS输出策略

    // 色彩范围设置 (参考MPP测试代码无默认设置, 使用ffmpeg兼容性)
    cfg.rc_color_range_override = 1;  // MPEG range (16-235)

    // 禁用高级特性以获得稳定性 (参考MPP测试代码默认禁用)
    cfg.rc_low_delay   = true;
    cfg.rc_super_mode  = 0;
    cfg.rc_debreath_en = false;
    cfg.rc_max_reenc_times = 1;

    // 自动修复ffmpeg兼容性
    MppEncoderContext::fixForFfmpeg(cfg);

    return cfg;
}

// 生成 JPEG 编码器配置
inline static MppEncoderContext::Config createJpegConfig(
    uint32_t width, uint32_t height,  MppFrameFormat format = MPP_FMT_YUV420SP,
    int quality = 8  // 0-10
) {
    MppEncoderContext::Config cfg;
    // ---- 基础配置 ----
    cfg.codec_type = MppEncoderContext::CodingType::MJPEG;
    
    // ---- PREP ----
    cfg.prep_width = width;
    cfg.prep_height = height;
    cfg.prep_format = format;
    cfg.prep_hor_stride = 0;  // 自动计算
    cfg.prep_ver_stride = 0;
    cfg.prep_rotation = MPP_ENC_ROT_0;
    cfg.prep_mirroring = 0;
    
    // ---- JPEG 质量参数 ----
    cfg.jpeg_q_factor = quality * 10;  // 转换为 0-100
    cfg.jpeg_qf_max = 99;
    cfg.jpeg_qf_min = 10;
    
    // ---- 禁用 H.264/HEVC 无关参数 ----
    cfg.rc_mode = MPP_ENC_RC_MODE_FIXQP;  // JPEG 不需要码率控制
    cfg.rc_fps_in_flex = 0;
    cfg.rc_fps_in_num = 1;   // JPEG 单帧,帧率无意义
    cfg.rc_fps_in_denorm = 1;
    cfg.rc_fps_out_flex = 0;
    cfg.rc_fps_out_num = 1;
    cfg.rc_fps_out_denorm = 1;
    cfg.rc_gop = 0;          // JPEG 无 GOP
    cfg.rc_bps_target = 0;   // JPEG 无码率控制
    cfg.rc_bps_max = 0;
    cfg.rc_bps_min = 0;
    
    // ---- SEI/Header ----
    cfg.sei_mode = MPP_ENC_SEI_MODE_DISABLE;
    cfg.header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    
    return cfg;
}

/// 获取常用分辨率视频录制配置
inline static MppEncoderContext::Config defconfig_480p_video(int fps=120) {
    return defconfig_video_recording(640, 480, fps, 0);
}

inline static MppEncoderContext::Config defconfig_720p_video(int fps=60) {
    return defconfig_video_recording(1280, 720, fps, 0);
}

inline static MppEncoderContext::Config defconfig_1080p_video(int fps=60) {
    return defconfig_video_recording(1920, 1080, fps, 0);
}

inline static MppEncoderContext::Config defconfig_4k_video(int fps=30) {
    return defconfig_video_recording(3840, 2160, fps, 25);
}

inline static MppEncoderContext::Config defconfig_2k_video(int fps=30) {
    return defconfig_video_recording(2560, 1440, fps, 10);
}
} // namespace DefaultConfigs