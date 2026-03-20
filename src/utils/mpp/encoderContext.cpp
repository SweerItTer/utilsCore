#include <cstdio>
#include <algorithm>
#include <string>

#include "logger_v2.h"
#include "mpp/encoderContext.h"

MppEncoderContext::MppEncoderContext(const Config &cfg)
    : mCurCfg(cfg) {
    if (init()) {
        LOG_INFO("[MppEncoderContext] Initialization succeeded.");
    } else {
        LOG_ERROR("[MppEncoderContext] Initialization failed!");
    }
}

MppEncoderContext::~MppEncoderContext() {
    if (nullptr != mCfg) {
        mpp_enc_cfg_deinit(mCfg);
        mCfg = nullptr;
    }
    if (nullptr != mCtx) {
        mpp_destroy(mCtx);
        mCtx = nullptr;
    }
}

bool MppEncoderContext::resetConfig(const Config &cfg) {
    if (!validateConfig(cfg)) {
        LOG_ERROR("[MppEncoderContext] resetConfig rejected invalid config.");
        return false;
    }
    mCurCfg = cfg;
    return applyConfig();
}

bool MppEncoderContext::init() {
    if (!validateConfig(mCurCfg)) {
        LOG_ERROR("[MppEncoderContext] init rejected invalid config.");
        return false;
    }
    if (MPP_OK != mpp_create(&mCtx, &mpi)) {
        LOG_ERROR("[MppEncoderContext] mpp_create failed");
        return false;
    }
    if (MPP_OK != mpp_init(mCtx, MPP_CTX_ENC, (MppCodingType)mCurCfg.codec_type)) {
        LOG_ERROR("[MppEncoderContext] mpp_init failed");
        mpp_destroy(mCtx);
        mCtx = nullptr;
        mpi = nullptr;
        return false;
    }
    if (MPP_OK != mpp_enc_cfg_init(&mCfg)) {
        LOG_ERROR("[MppEncoderContext] mpp_enc_cfg_init failed");
        mpp_destroy(mCtx);
        mCtx = nullptr;
        mpi = nullptr;
        return false;
    }

    return applyConfig();
}

bool MppEncoderContext::validateConfig(const Config& cfg) {
    if (cfg.prep_width <= 0 || cfg.prep_height <= 0) {
        LOG_ERROR("[MppEncoderContext] Invalid frame size: %dx%d", cfg.prep_width, cfg.prep_height);
        return false;
    }
    if (cfg.prep_hor_stride < 0 || cfg.prep_ver_stride < 0) {
        LOG_ERROR("[MppEncoderContext] Stride must not be negative.");
        return false;
    }
    if (cfg.prep_hor_stride > 0 && cfg.prep_hor_stride < cfg.prep_width) {
        LOG_ERROR("[MppEncoderContext] Horizontal stride %d is smaller than width %d.",
                  cfg.prep_hor_stride,
                  cfg.prep_width);
        return false;
    }
    if (cfg.prep_ver_stride > 0 && cfg.prep_ver_stride < cfg.prep_height) {
        LOG_ERROR("[MppEncoderContext] Vertical stride %d is smaller than height %d.",
                  cfg.prep_ver_stride,
                  cfg.prep_height);
        return false;
    }
    if (cfg.packet_poll_retries <= 0) {
        LOG_ERROR("[MppEncoderContext] packet_poll_retries must be positive.");
        return false;
    }
    if (cfg.packet_poll_interval_us <= 0) {
        LOG_ERROR("[MppEncoderContext] packet_poll_interval_us must be positive.");
        return false;
    }
    if (cfg.packet_ready_timeout_us <= 0) {
        LOG_ERROR("[MppEncoderContext] packet_ready_timeout_us must be positive.");
        return false;
    }
    if (cfg.prep_format == MPP_FMT_BUTT) {
        LOG_ERROR("[MppEncoderContext] prep_format is invalid.");
        return false;
    }
    return true;
}

bool MppEncoderContext::applyConfig() {
    if (nullptr == mCfg || nullptr == mCtx || nullptr == mpi)
        return false;
    if (!validateConfig(mCurCfg)) {
        return false;
    }

    // ---- Helper lambda: 设置单个 MPP 配置项 ----
    auto setConfigValue = [&](const char* key, RK_S32 value) -> bool {
        MPP_RET ret = mpp_enc_cfg_set_s32(mCfg, key, value);
        if (MPP_OK != ret) {
            LOG_ERROR("[MppEncoderContext] ERROR: set %s failed, ret=%d", key, ret);
            return false;
        }
        return true;
    };

    auto setOptionalConfigValue = [&](const char* key, RK_S32 value) -> bool {
        MPP_RET ret = mpp_enc_cfg_set_s32(mCfg, key, value);
        if (MPP_OK != ret) {
            LOG_WARN("[MppEncoderContext] Optional config %s unsupported, skip it. ret=%d", key, ret);
            return true;
        }
        return true;
    };

    // 判断是否为 MJPEG 编码器
    bool is_mjpeg = (static_cast<MppCodingType>(mCurCfg.codec_type) == MPP_VIDEO_CodingMJPEG);
    
    // 检查配置
    if (!is_mjpeg) validateForFfmpeg(mCurCfg);

    // 自动计算 stride (参考MPP测试代码)
    // int hor_stride = mCurCfg.prep_hor_stride ? mCurCfg.prep_hor_stride : ((mCurCfg.prep_width + 15) & ~15);
    // int ver_stride = mCurCfg.prep_ver_stride ? mCurCfg.prep_ver_stride : ((mCurCfg.prep_height + 15) & ~15);

    int hor_stride = mCurCfg.prep_hor_stride ? mCurCfg.prep_hor_stride : mCurCfg.prep_width;
    int ver_stride = mCurCfg.prep_ver_stride ? mCurCfg.prep_ver_stride : mCurCfg.prep_height;
    // ---- PREP / 输入图像预处理 (参考MPP测试代码) ----
    if (!setConfigValue("prep:width", mCurCfg.prep_width) ||
        !setConfigValue("prep:height", mCurCfg.prep_height) ||
        !setConfigValue("prep:hor_stride", hor_stride) ||
        !setConfigValue("prep:ver_stride", ver_stride) ||
        !setConfigValue("prep:format", mCurCfg.prep_format) ||
        !setConfigValue("prep:rotation", mCurCfg.prep_rotation) ||
        !setConfigValue("prep:mirroring", mCurCfg.prep_mirroring)) {
        return false;
    }

    // ---- RC / 码率控制 (MJPEG 跳过大部分) ----
    if (!setConfigValue("rc:mode", mCurCfg.rc_mode)) {
        return false;
    }

    if (!is_mjpeg) {
        // 固定输入/输出帧率 (MJPEG 单帧编码,跳过)
        if (!setConfigValue("rc:fps_in_flex", mCurCfg.rc_fps_in_flex) ||
            !setConfigValue("rc:fps_in_num", mCurCfg.rc_fps_in_num) ||
            !setConfigValue("rc:fps_in_denorm", mCurCfg.rc_fps_in_denorm) ||
            !setConfigValue("rc:fps_out_flex", mCurCfg.rc_fps_out_flex) ||
            !setConfigValue("rc:fps_out_num", mCurCfg.rc_fps_out_num) ||
            !setConfigValue("rc:fps_out_denorm", mCurCfg.rc_fps_out_denorm)) {
            return false;
        }

        // GOP设置 (MJPEG 无 GOP)
        if (!setConfigValue("rc:gop", mCurCfg.rc_gop ? mCurCfg.rc_gop : mCurCfg.rc_fps_out_num * 2)) {
            return false;
        }

        // 丢帧控制 (MJPEG 跳过)
        if (!setConfigValue("rc:drop_mode", MPP_ENC_RC_DROP_FRM_DISABLED) ||
            !setConfigValue("rc:drop_thd", 20) ||
            !setConfigValue("rc:drop_gap", 1)) {
            return false;
        }

        // 码率设置 (MJPEG 使用质量因子,跳过)
        if (!setConfigValue("rc:bps_target", mCurCfg.rc_bps_target)) {
            return false;
        }

        // 根据RC模式设置码率范围
        switch (mCurCfg.rc_mode) {
        case MPP_ENC_RC_MODE_CBR:
            if (!setConfigValue("rc:bps_max", mCurCfg.rc_bps_max ? mCurCfg.rc_bps_max : mCurCfg.rc_bps_target * 17 / 16) ||
                !setConfigValue("rc:bps_min", mCurCfg.rc_bps_min ? mCurCfg.rc_bps_min : mCurCfg.rc_bps_target * 15 / 16)) {
                return false;
            }
            break;
        case MPP_ENC_RC_MODE_VBR:
        case MPP_ENC_RC_MODE_AVBR:
            if (!setConfigValue("rc:bps_max", mCurCfg.rc_bps_max ? mCurCfg.rc_bps_max : mCurCfg.rc_bps_target * 17 / 16) ||
                !setConfigValue("rc:bps_min", mCurCfg.rc_bps_min ? mCurCfg.rc_bps_min : mCurCfg.rc_bps_target * 1 / 16)) {
                return false;
            }
            break;
        case MPP_ENC_RC_MODE_FIXQP:
            // FIXQP模式不设置码率
            break;
        default:
            if (!setConfigValue("rc:bps_max", mCurCfg.rc_bps_max ? mCurCfg.rc_bps_max : mCurCfg.rc_bps_target * 17 / 16) ||
                !setConfigValue("rc:bps_min", mCurCfg.rc_bps_min ? mCurCfg.rc_bps_min : mCurCfg.rc_bps_target * 15 / 16)) {
                return false;
            }
            break;
        }

        // 强制 I 帧 / GOP (MJPEG 跳过)
        if (mCurCfg.rc_force_idr_interval > 0 &&
            !setOptionalConfigValue("rc:force_idr_interval", mCurCfg.rc_force_idr_interval)) {
            return false;
        }
    }

    // QP设置 (参考MPP测试代码)
    switch (static_cast<MppCodingType>(mCurCfg.codec_type)) {
    case MPP_VIDEO_CodingAVC:
    case MPP_VIDEO_CodingHEVC:
        switch (mCurCfg.rc_mode) {
        case MPP_ENC_RC_MODE_FIXQP:
            if (!setConfigValue("rc:qp_init", mCurCfg.rc_qp_init) ||
                !setConfigValue("rc:qp_max", mCurCfg.rc_qp_init) ||
                !setConfigValue("rc:qp_min", mCurCfg.rc_qp_init) ||
                !setConfigValue("rc:qp_max_i", mCurCfg.rc_qp_init) ||
                !setConfigValue("rc:qp_min_i", mCurCfg.rc_qp_init) ||
                !setConfigValue("rc:qp_ip", 0)) {
                return false;
            }
            break;
        case MPP_ENC_RC_MODE_CBR:
        case MPP_ENC_RC_MODE_VBR:
        case MPP_ENC_RC_MODE_AVBR:
        default:
            if (!setConfigValue("rc:qp_init", mCurCfg.rc_qp_init >= 0 ? mCurCfg.rc_qp_init : -1) ||
                !setConfigValue("rc:qp_max", mCurCfg.rc_qp_max > 0 ? mCurCfg.rc_qp_max : 51) ||
                !setConfigValue("rc:qp_min", mCurCfg.rc_qp_min > 0 ? mCurCfg.rc_qp_min : 10) ||
                !setConfigValue("rc:qp_max_i", mCurCfg.rc_qp_max_i > 0 ? mCurCfg.rc_qp_max_i : 51) ||
                !setConfigValue("rc:qp_min_i", mCurCfg.rc_qp_min_i > 0 ? mCurCfg.rc_qp_min_i : 10) ||
                !setConfigValue("rc:qp_ip", mCurCfg.rc_qp_ip > 0 ? mCurCfg.rc_qp_ip : 2)) {
                return false;
            }
            break;
        }
        break;
    case MPP_VIDEO_CodingVP8:
        if (!setConfigValue("rc:qp_init", 40) ||
            !setConfigValue("rc:qp_max", 127) ||
            !setConfigValue("rc:qp_min", 0) ||
            !setConfigValue("rc:qp_max_i", 127) ||
            !setConfigValue("rc:qp_min_i", 0) ||
            !setConfigValue("rc:qp_ip", 6)) {
            return false;
        }
        break;
    case MPP_VIDEO_CodingMJPEG:
        // JPEG使用质量因子 (0-100)
        if (!setConfigValue("jpeg:q_factor", mCurCfg.jpeg_q_factor) ||
            !setConfigValue("jpeg:qf_max", mCurCfg.jpeg_qf_max) ||
            !setConfigValue("jpeg:qf_min", mCurCfg.jpeg_qf_min)) {
            return false;
        }
        break;
    default:
        LOG_ERROR("[MppEncoderContext] ERROR: unsupported codec type %d", mCurCfg.codec_type);
        return false;
    }

    // ---- Codec / 编码器特定参数 ----
    if (!setConfigValue("codec:type", static_cast<MppCodingType>(mCurCfg.codec_type))) {
        return false;
    }
    switch (static_cast<MppCodingType>(mCurCfg.codec_type)) {
        case MPP_VIDEO_CodingAVC:
            if (!setConfigValue("h264:profile", mCurCfg.h264_profile) ||
                !setConfigValue("h264:level", mCurCfg.h264_level) ||
                !setConfigValue("h264:cabac_en", mCurCfg.cabac_enable ? 1 : 0) ||
                !setConfigValue("h264:cabac_idc", mCurCfg.cabac_idc) ||
                !setConfigValue("h264:trans8x8", 1)) {
                return false;
            }
            break;
        case MPP_VIDEO_CodingHEVC:
            if (!setConfigValue("hevc:profile", mCurCfg.hevc_profile) ||
                !setConfigValue("hevc:level", mCurCfg.hevc_level)) {
                return false;
            }
            break;
        case MPP_VIDEO_CodingMJPEG:
            // JPEG 参数已在上面设置,这里跳过
            break;
        case MPP_VIDEO_CodingVP8:
            // VP8配置已在QP设置中处理
            break;
        default:
            LOG_ERROR("[MppEncoderContext] ERROR: unsupported codec type %d", mCurCfg.codec_type);
            return false;
    }

    // ---- SEI / header 输出 (MJPEG 跳过) ----
    if (!is_mjpeg) {
        if (MPP_OK != mpi->control(mCtx, MPP_ENC_SET_SEI_CFG, &mCurCfg.sei_mode)) {
            LOG_ERROR("[MppEncoderContext] ERROR: MPP_ENC_SET_SEI_CFG failed");
            return false;
        }
        MppCodingType type_ = static_cast<MppCodingType>(mCurCfg.codec_type);
        if ((type_ == MPP_VIDEO_CodingAVC || type_ == MPP_VIDEO_CodingHEVC) &&
            MPP_OK != mpi->control(mCtx, MPP_ENC_SET_HEADER_MODE, &mCurCfg.header_mode)) {
            LOG_ERROR("[MppEncoderContext] ERROR: MPP_ENC_SET_HEADER_MODE failed");
            return false;
        }
    }

    // ---- 色彩范围 ----
    if (mCurCfg.rc_color_range_override >= 0 &&
        !setOptionalConfigValue("rc:color_range_override", mCurCfg.rc_color_range_override)) {
        return false;
    }

    // ---- 应用全部配置 ----
    if (MPP_OK != mpi->control(mCtx, MPP_ENC_SET_CFG, mCfg)) {
        LOG_ERROR("[MppEncoderContext] ERROR: MPP_ENC_SET_CFG failed");
        return false;
    }

    return true;
}
