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
#include <QSize>

#include "types.h"
#include "rga/rgaProcessor.h"

class PlayThread : public QThread
{
	Q_OBJECT
public:	
	explicit PlayThread(QObject *parent = nullptr,
					std::shared_ptr<FrameQueue> frameQueue = nullptr,
					std::shared_ptr<RgaProcessor> rgaProcessor = nullptr,
					QSize size = QSize());
	~PlayThread(){
		stopCapture();
	}

	void startCapture();
	void stopCapture();

	void retuenBuff(int index);

signals:
    void frameReady(const void* data, const QSize& size, int index);     // for MMAP
    void frameReadyDmabuf(const int fd, const QSize& size, const int index); // for DMABUF

protected:
	void run() override;

private:
	std::atomic_bool running;
	std::shared_ptr<FrameQueue> frameQueue_;
	std::shared_ptr<RgaProcessor> rgaProcessor_;
	QSize size_;
};

#endif // PLAYTHREAD_H
