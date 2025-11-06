/*
 * @FilePath: /EdgeVision/include/utils/types.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-17 23:32:08
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef TYPES_H
#define TYPES_H

#include "safeQueue.h"
#include "concurrentqueue.h"
#include "v4l2/frame.h"

using FramePtr = std::shared_ptr<Frame>;
// using FrameQueue = SafeQueue<FramePtr>;
using FrameQueue = moodycamel::ConcurrentQueue<FramePtr>;

#endif // TYPES_H