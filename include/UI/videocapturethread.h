#ifndef VIDEOCAPTURETHREAD_H
#define VIDEOCAPTURETHREAD_H

#include <QObject>
#include <QThread>
#include <QImage>

#include <opencv2/opencv.hpp>

class VideoCaptureThread : public QThread
{
	Q_OBJECT
public:
	explicit VideoCaptureThread(QObject *parent = nullptr);
	~VideoCaptureThread(){
		stopCapture();
	}

	void startCapture(const int index);
	void stopCapture();

signals:
	void frameReady(const QImage& img);

protected:
	void run() override;

private:
	std::atomic_bool m_isRunning;
	cv::VideoCapture cap;
};

#endif // VIDEOCAPTURETHREAD_H
