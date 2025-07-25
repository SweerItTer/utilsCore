/*
 * @FilePath: /EdgeVision/include/UI/MMAP/playthread.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-16 23:34:00
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef PLAYTHREAD_H
#define PLAYTHREAD_H

#include <memory>

#include <QObject>
#include <QThread>
#include <QImage>

#include "types.h"
#include "rga/rgaProcessor.h"

class PlayThread : public QThread
{
	Q_OBJECT
public:	
	explicit PlayThread(QObject *parent = nullptr,
					std::shared_ptr<FrameQueue> frameQueue = nullptr,
					std::shared_ptr<RgaProcessor> rgaProcessor = nullptr,
					int width = 0, int height = 0);
	~PlayThread(){
		stopCapture();
	}

	void startCapture();
	void stopCapture();

signals:
	void frameReady(const QImage& img);

protected:
	void run() override;

private:
	std::atomic_bool running;
	std::shared_ptr<FrameQueue> frameQueue_;
	std::shared_ptr<RgaProcessor> rgaProcessor_;
	int width_;
	int height_;
};

#endif // PLAYTHREAD_H
