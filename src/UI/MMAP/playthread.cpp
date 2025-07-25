/*
 * @FilePath: /EdgeVision/src/UI/MMAP/playthread.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-16 23:33:33
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "MMAP/playthread.h"

#include <iostream>

PlayThread::PlayThread(QObject *parent,
					std::shared_ptr<FrameQueue> frameQueue, std::shared_ptr<RgaProcessor> rgaProcessor,
					int width, int height)
	: QThread(parent), frameQueue_(frameQueue)
	, rgaProcessor_(rgaProcessor)
	, width_(width), height_(height), running(false)
{}

void PlayThread::startCapture()
{
	if (true == running) return;
	running = true;
	start();
}

void PlayThread::stopCapture()
{
	running = false;
	wait(); // 等待线程结束
}

void PlayThread::run()
{
	QImage img;
	Frame frame(nullptr,0,0,-1);
	while( true == running ){
		// 取出数据
		if(frameQueue_->try_dequeue(frame)){
			void* data = frame.data();
            if (nullptr == data) {
                rgaProcessor_->releaseBuffer(frame.index());
                continue;
            }
            
            img = QImage( 
                static_cast<const uchar*>(data),
                width_,
                height_,
                QImage::Format_RGBA8888  // 匹配 RGA 输出的 RGBA 格式
            );
			// 归还处理后buffer
			rgaProcessor_->releaseBuffer(frame.index());
			if( running != true ) break;
			// fprintf(stdout, "emit frameReady\n");
			// 加入缓存队列
			emit frameReady(img.copy());
		} else {
			// std::cout << "frame is empty" << std::endl;
			//避免空转消耗 CPU
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

	}
}
