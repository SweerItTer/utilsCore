/*
 * @FilePath: /EdgeVision/include/utils/types.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-17 23:32:08
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef TYPES_H
#define TYPES_H

#include "safeQueue.h"
#include "v4l2/frame.h"
using FrameQueue = SafeQueue<std::unique_ptr<Frame>>;

#endif // TYPES_H