/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2026-01-12 14:38:50
 * @FilePath: /EdgeVision/include/pipeline/recordPipeline.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once
#include <iomanip>

#include "v4l2/cameraController.h"  // 摄像头捕获

#include "mpp/encoderCore.h"
#include "mpp/streamWriter.h"

#include "threadPauser.h"
#include "threadUtils.h"

#include "rga/rgaConverter.h"
#include "rga/formatTool.h"

class RecordPipeline {
public:
    RecordPipeline();
    ~RecordPipeline();

    // 设置与约束
    void setResolution(int w, int h); // 内部进行 clamp(640, 1920)
    void setSavePath(const std::string& path);

    // 控制接口
    void start();
    void pause();
    void resume();
    void stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};