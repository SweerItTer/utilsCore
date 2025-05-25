#include "rkmedia_s.h"

#include <iostream>

namespace RKAPIs {
    MediaSys::MediaSys()
    {   
        ret = RK_MPI_SYS_Init();
        if(ret != RK_SUCCESS) {
            std::cerr << "Error: RK_MPI_SYS_Init failed" << std::endl;
        }
    }

    MediaSys::~MediaSys()
    {
        RK_MPI_SYS_UnBind(Vi_Pipe, ViChn);
        delete Vi_Pipe;
        delete ViChn;
    }

    int MediaSys::BindChn(MPP_CHN_S *pipe, MPP_CHN_S *chn)
    {
        Vi_Pipe = pipe;
        ViChn = chn;
        return RK_MPI_SYS_Bind(Vi_Pipe, ViChn);
    }

    VIChannel::VIChannel(int cam_id, int chn_id, const Config &cfg)
        : cam_id_(cam_id), chn_id_(chn_id)
    {
        VI_CHN_ATTR_S attr; 
        attr.pcVideoNode = cfg.video_node;
        attr.u32BufCnt = cfg.buffer_count;
        attr.u32Width = cfg.width;
        attr.u32Height = cfg.height;
        attr.enPixFmt = cfg.pix_fmt;
        attr.enWorkMode = cfg.work_mode;

        if(RK_MPI_VI_SetChnAttr(cam_id_, chn_id_, &attr) != RK_SUCCESS ||
            RK_MPI_VI_EnableChn(cam_id_, chn_id_) != RK_SUCCESS) 
        {
            throw std::runtime_error("Create VI channel failed");
        }
    }

    VIChannel::~VIChannel()
    {
        RK_MPI_VI_DisableChn(cam_id_, chn_id_);
    }

}