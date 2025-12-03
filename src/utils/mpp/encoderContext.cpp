#include <cstdio>
#include <string>

#include "mpp/encoderContext.h"

MppEncoderContext::MppEncoderContext(const Config &cfg)
    : mCurCfg(cfg) {
    if (init()) {
        fprintf(stderr, "[MppEncoderContext] Initialization successed!\n");\
    } else {
        fprintf(stderr, "[MppEncoderContext] Initialization failed!\n");
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
    mCurCfg = cfg;
    return applyConfig();
}

bool MppEncoderContext::init() {
    if (MPP_OK != mpp_create(&mCtx, &mpi)) {
        fprintf(stderr, "[MppEncoderContext] mpp_create failed\n");
        return false;
    }
    if (MPP_OK != mpp_init(mCtx, MPP_CTX_ENC, (MppCodingType)mCurCfg.codec_type)) {
        fprintf(stderr, "[MppEncoderContext] mpp_init failed\n");
        return false;
    }
    if (MPP_OK != mpp_enc_cfg_init(&mCfg)) {
        fprintf(stderr, "[MppEncoderContext] mpp_enc_cfg_init failed\n");
        return false;
    }

    return applyConfig();
}

bool MppEncoderContext::applyConfig() {
    if (nullptr == mCfg || nullptr == mCtx || nullptr == mpi)
        return false;

    // ---- Helper lambda: 设置单个 MPP 配置项 ----
    auto set_cfg = [&](const char* key, RK_S32 value) -> bool {
        MPP_RET ret = mpp_enc_cfg_set_s32(mCfg, key, value);
        if (MPP_OK != ret) {
            fprintf(stderr, "[MppEncoderContext] ERROR: set %s failed, ret=%d\n", key, ret);
            return false;
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
    set_cfg("prep:width", mCurCfg.prep_width);
    set_cfg("prep:height", mCurCfg.prep_height);
    set_cfg("prep:hor_stride", hor_stride);
    set_cfg("prep:ver_stride", ver_stride);
    set_cfg("prep:format", mCurCfg.prep_format);
    set_cfg("prep:rotation", mCurCfg.prep_rotation);
    set_cfg("prep:mirroring", mCurCfg.prep_mirroring);

    // ---- RC / 码率控制 (MJPEG 跳过大部分) ----
    set_cfg("rc:mode", mCurCfg.rc_mode);

    if (!is_mjpeg) {
        // 固定输入/输出帧率 (MJPEG 单帧编码,跳过)
        set_cfg("rc:fps_in_flex", mCurCfg.rc_fps_in_flex);
        set_cfg("rc:fps_in_num", mCurCfg.rc_fps_in_num);
        set_cfg("rc:fps_in_denorm", mCurCfg.rc_fps_in_denorm);
        set_cfg("rc:fps_out_flex", mCurCfg.rc_fps_out_flex);
        set_cfg("rc:fps_out_num", mCurCfg.rc_fps_out_num);
        set_cfg("rc:fps_out_denorm", mCurCfg.rc_fps_out_denorm);

        // GOP设置 (MJPEG 无 GOP)
        set_cfg("rc:gop", mCurCfg.rc_gop ? mCurCfg.rc_gop : mCurCfg.rc_fps_out_num * 2);

        // 丢帧控制 (MJPEG 跳过)
        set_cfg("rc:drop_mode", MPP_ENC_RC_DROP_FRM_DISABLED);
        set_cfg("rc:drop_thd", 20);
        set_cfg("rc:drop_gap", 1);

        // 码率设置 (MJPEG 使用质量因子,跳过)
        set_cfg("rc:bps_target", mCurCfg.rc_bps_target);

        // 根据RC模式设置码率范围
        switch (mCurCfg.rc_mode) {
        case MPP_ENC_RC_MODE_CBR:
            set_cfg("rc:bps_max", mCurCfg.rc_bps_max ? mCurCfg.rc_bps_max : mCurCfg.rc_bps_target * 17 / 16);
            set_cfg("rc:bps_min", mCurCfg.rc_bps_min ? mCurCfg.rc_bps_min : mCurCfg.rc_bps_target * 15 / 16);
            break;
        case MPP_ENC_RC_MODE_VBR:
        case MPP_ENC_RC_MODE_AVBR:
            set_cfg("rc:bps_max", mCurCfg.rc_bps_max ? mCurCfg.rc_bps_max : mCurCfg.rc_bps_target * 17 / 16);
            set_cfg("rc:bps_min", mCurCfg.rc_bps_min ? mCurCfg.rc_bps_min : mCurCfg.rc_bps_target * 1 / 16);
            break;
        case MPP_ENC_RC_MODE_FIXQP:
            // FIXQP模式不设置码率
            break;
        default:
            set_cfg("rc:bps_max", mCurCfg.rc_bps_max ? mCurCfg.rc_bps_max : mCurCfg.rc_bps_target * 17 / 16);
            set_cfg("rc:bps_min", mCurCfg.rc_bps_min ? mCurCfg.rc_bps_min : mCurCfg.rc_bps_target * 15 / 16);
            break;
        }

        // 强制 I 帧 / GOP (MJPEG 跳过)
        if (mCurCfg.rc_force_idr_interval > 0)
            set_cfg("rc:force_idr_interval", mCurCfg.rc_force_idr_interval);
    }

    // QP设置 (参考MPP测试代码)
    switch (static_cast<MppCodingType>(mCurCfg.codec_type)) {
    case MPP_VIDEO_CodingAVC:
    case MPP_VIDEO_CodingHEVC:
        switch (mCurCfg.rc_mode) {
        case MPP_ENC_RC_MODE_FIXQP:
            set_cfg("rc:qp_init", mCurCfg.rc_qp_init);
            set_cfg("rc:qp_max", mCurCfg.rc_qp_init);
            set_cfg("rc:qp_min", mCurCfg.rc_qp_init);
            set_cfg("rc:qp_max_i", mCurCfg.rc_qp_init);
            set_cfg("rc:qp_min_i", mCurCfg.rc_qp_init);
            set_cfg("rc:qp_ip", 0);
            break;
        case MPP_ENC_RC_MODE_CBR:
        case MPP_ENC_RC_MODE_VBR:
        case MPP_ENC_RC_MODE_AVBR:
        default:
            set_cfg("rc:qp_init", mCurCfg.rc_qp_init >= 0 ? mCurCfg.rc_qp_init : -1);
            set_cfg("rc:qp_max", mCurCfg.rc_qp_max > 0 ? mCurCfg.rc_qp_max : 51);
            set_cfg("rc:qp_min", mCurCfg.rc_qp_min > 0 ? mCurCfg.rc_qp_min : 10);
            set_cfg("rc:qp_max_i", mCurCfg.rc_qp_max_i > 0 ? mCurCfg.rc_qp_max_i : 51);
            set_cfg("rc:qp_min_i", mCurCfg.rc_qp_min_i > 0 ? mCurCfg.rc_qp_min_i : 10);
            set_cfg("rc:qp_ip", mCurCfg.rc_qp_ip > 0 ? mCurCfg.rc_qp_ip : 2);
            break;
        }
        break;
    case MPP_VIDEO_CodingVP8:
        set_cfg("rc:qp_init", 40);
        set_cfg("rc:qp_max", 127);
        set_cfg("rc:qp_min", 0);
        set_cfg("rc:qp_max_i", 127);
        set_cfg("rc:qp_min_i", 0);
        set_cfg("rc:qp_ip", 6);
        break;
    case MPP_VIDEO_CodingMJPEG:
        // JPEG使用质量因子 (0-100)
        set_cfg("jpeg:q_factor", mCurCfg.jpeg_q_factor);
        set_cfg("jpeg:qf_max", mCurCfg.jpeg_qf_max);
        set_cfg("jpeg:qf_min", mCurCfg.jpeg_qf_min);
        break;
    default:
        fprintf(stderr, "[MppEncoderContext] ERROR: unsupported codec type %d\n", mCurCfg.codec_type);
        return false;
    }

    // ---- Codec / 编码器特定参数 ----
    set_cfg("codec:type", static_cast<MppCodingType>(mCurCfg.codec_type));
    switch (static_cast<MppCodingType>(mCurCfg.codec_type)) {
        case MPP_VIDEO_CodingAVC:
            set_cfg("h264:profile", mCurCfg.h264_profile);
            set_cfg("h264:level", mCurCfg.h264_level);
            set_cfg("h264:cabac_en", mCurCfg.cabac_enable ? 1 : 0);
            set_cfg("h264:cabac_idc", mCurCfg.cabac_idc);
            set_cfg("h264:trans8x8", 1);
            break;
        case MPP_VIDEO_CodingHEVC:
            set_cfg("hevc:profile", mCurCfg.hevc_profile);
            set_cfg("hevc:level", mCurCfg.hevc_level);
            break;
        case MPP_VIDEO_CodingMJPEG:
            // JPEG 参数已在上面设置,这里跳过
            break;
        case MPP_VIDEO_CodingVP8:
            // VP8配置已在QP设置中处理
            break;
        default:
            fprintf(stderr, "[MppEncoderContext] ERROR: unsupported codec type %d\n", mCurCfg.codec_type);
            return false;
    }

    // ---- SEI / header 输出 (MJPEG 跳过) ----
    if (!is_mjpeg) {
        mpi->control(mCtx, MPP_ENC_SET_SEI_CFG, &mCurCfg.sei_mode);
        MppCodingType type_ = static_cast<MppCodingType>(mCurCfg.codec_type);
        if (type_ == MPP_VIDEO_CodingAVC || type_ == MPP_VIDEO_CodingHEVC)
            mpi->control(mCtx, MPP_ENC_SET_HEADER_MODE, &mCurCfg.header_mode);
    }

    // ---- 色彩范围 ----
    if (mCurCfg.rc_color_range_override >= 0)
        set_cfg("rc:color_range_override", mCurCfg.rc_color_range_override);

    // ---- 应用全部配置 ----
    if (MPP_OK != mpi->control(mCtx, MPP_ENC_SET_CFG, mCfg)) {
        fprintf(stderr, "[MppEncoderContext] ERROR: MPP_ENC_SET_CFG failed\n");
        return false;
    }

    return true;
}