/*
 * @FilePath: /EdgeVision/src/UI/MMAP/playthread.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-16 23:33:33
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "MMAP/playthread.h"
#include <QImage>
#include <iostream>

#include "logger.h"

PlayThread::PlayThread(QObject *parent,
					std::shared_ptr<FrameQueue> frameQueue,
					std::shared_ptr<RgaProcessor> rgaProcessor,
					QSize size)
	: QThread(parent), frameQueue_(frameQueue)
	, rgaProcessor_(rgaProcessor)
	, size_(size)
	, running(false), paused(false)
{}

void PlayThread::startCapture()
{
	if (true == paused) {
		paused = false;
	}
	
	if (true == running) return;
	running = true;
	start();
}

void PlayThread::stopCapture()
{
	running = false;
	wait(); // 等待线程结束
}

void PlayThread::pause(){
	paused = true;
}

void PlayThread::retuenBuff(const int index)
{
	// 后续槽函数处理完成后调用以回收资源(不管是fd还是ptr)
	int index_ = index;
	rgaProcessor_->releaseBuffer(index_);
}

void PlayThread::run()
{
	Frame frame;
	while( true == running ){
		if ( true == paused ) {
			this->sleep(10);
			if ( false == running ) break;
		}
		// 取出数据 后续可做图像处理什么的
		if(frameQueue_->try_dequeue(frame)){
			uint64_t t3 = mk::timeDiffMs(frame.timestamp(), "[FrameDqueue]");
			
			if (Frame::MemoryType::DMABUF == frame.type()){
				// 转发 dmabuf
				emit frameReadyDmabuf(frame.dmabuf_fd(), size_, t3, frame.index());
			} else {
				// 转发 mmap 数据裸指针
				emit frameReady(frame.data(), size_, t3, frame.index());
			}
		} else {
			// std::cout << "frame is empty" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

	}
}
