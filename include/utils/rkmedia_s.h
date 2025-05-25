// 对rkmedia的二次封装

#ifndef RKMEDIA_S_H
#define RKMEDIA_S_H

#include "rkmedia/rkmedia_api.h"

namespace RKAPIs {
// sys init
    class MediaSys
    {
    private:
        int ret;
        MPP_CHN_S *Vi_Pipe = nullptr;
        MPP_CHN_S *ViChn = nullptr;
    public:
        explicit MediaSys();
        ~MediaSys();

        int BindChn(MPP_CHN_S *pipe, MPP_CHN_S *chn);
        
        // 禁用拷贝和赋值
        MediaSys(const MediaSys&) = delete;
        MediaSys& operator=(const MediaSys&) = delete;
    };

// VI 
class VIChannel {
public:
    struct Config {
        const RK_CHAR *video_node = "rkispp_scale0"; // rkispp_scale0 etc.
        RK_U32 buffer_count = 3;
        RK_U32 width;
        RK_U32 height;
        IMAGE_TYPE_E pix_fmt = IMAGE_TYPE_NV12;
        VI_CHN_WORK_MODE work_mode = VI_WORK_MODE_NORMAL;
    };

    VIChannel(int cam_id, int chn_id, const Config &cfg);
    ~VIChannel();

    MPP_CHN_S get_bind_info() const {
        return {RK_ID_VI, cam_id_, chn_id_};
    }
private:
    const int cam_id_;
    const int chn_id_;
};
// RGA

// bind channel
}


#endif // !RKMEDIA_S_H