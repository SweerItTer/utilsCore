/*
 * @FilePath: /utilsCore/examples/rga_dmabuf_demo.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-02-22
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 * @Description: RgaProcessor DMABUF 演示：构造一帧 DMABUF -> RGA 转换 -> dump 到文件
 */

#include "rga/rgaProcessor.h"
#include "rga/formatTool.h"
#include "drm/deviceController.h"

#include <cstdio>
#include <memory>

static void fillSolidBGRA(const DmaBufferPtr& buf, uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
    if (!buf) return;
    auto view = buf->scopedMap();
    const uint32_t w = buf->width();
    const uint32_t h = buf->height();
    const uint32_t pitch = buf->pitch();

    for (uint32_t y = 0; y < h; ++y) {
        uint8_t* row = view.get() + y * pitch;
        for (uint32_t x = 0; x < w; ++x) {
            row[x * 4 + 0] = b;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = r;
            row[x * 4 + 3] = a;
        }
    }
}

int main(int argc, char** argv) {
    DrmDev::fd_ptr = DeviceController::create();
    const uint32_t w = 64;
    const uint32_t h = 64;
    const char* outPath = (argc >= 2) ? argv[1] : "rga_out.rgba";

    const int srcFmt = RK_FORMAT_BGRA_8888;
    const int dstFmt = RK_FORMAT_RGBA_8888;

    const uint32_t srcDrm = convertRGAtoDrmFormat(srcFmt);
    if (srcDrm == static_cast<uint32_t>(-1)) {
        std::fprintf(stderr, "convertRGAtoDrmFormat(srcFmt) failed\n");
        return 1;
    }

    auto src = DmaBuffer::create(w, h, srcDrm, 0, 0);
    if (!src || src->fd() < 0) {
        std::fprintf(stderr, "DmaBuffer::create() failed\n");
        return 1;
    }

    fillSolidBGRA(src, 0, 0, 255, 255);

    auto q = std::make_shared<FrameQueue>();
    auto inState = std::make_shared<SharedBufferState>(src, nullptr, 0);
    auto inFrame = std::make_shared<Frame>(inState);
    inFrame->meta.w = w;
    inFrame->meta.h = h;
    q->enqueue(inFrame);

    utils::rga::RgaProcessorConfig cfg;
    cfg.rawQueue = q;
    cfg.width = w;
    cfg.height = h;
    cfg.usingDMABUF = true;
    cfg.srcFormat = srcFmt;
    cfg.dstFormat = dstFmt;
    cfg.poolSize = 2;
    cfg.maxPendingTasks = 8;

    utils::rga::RgaProcessor proc(cfg);
    if (proc.start() != utils::rga::RgaProcessorError::SUCCESS) {
        std::fprintf(stderr, "RgaProcessor start failed\n");
        return 1;
    }

    FramePtr outFrame;
    const int idx = proc.dump(outFrame, 50000);
    if (idx < 0 || !outFrame) {
        std::fprintf(stderr, "dump() failed (idx=%d)\n", idx);
        proc.stop();
        return 1;
    }

    auto outState = outFrame->sharedState(0);
    if (!outState || !outState->dmabuf_ptr) {
        std::fprintf(stderr, "output state invalid\n");
        proc.stop();
        return 1;
    }

    const auto& outBuf = outState->dmabuf_ptr;
    const bool ok = utils::rga::RgaProcessor::dumpDmabufAsXXXX8888(
        outBuf->fd(),
        w,
        h,
        static_cast<uint32_t>(outBuf->size64()),
        outBuf->pitch(),
        outPath);

    proc.stop();

    if (!ok) {
        std::fprintf(stderr, "dumpDmabufAsXXXX8888 failed\n");
        return 1;
    }

    std::printf("Wrote raw RGBA8888 to %s (%ux%u).\n", outPath, w, h);
    std::printf("Tip: view as raw RGBA (e.g. ffplay -f rawvideo -pix_fmt rgba -s 64x64 %s)\n", outPath);
    return 0;
}

