#include "videocapturethread.h"
#include <QElapsedTimer>  // 添加头文件

VideoCaptureThread::VideoCaptureThread(QObject *parent)
	: QThread(parent), m_isRunning(false)
{
}

void VideoCaptureThread::startCapture(const int index)
{
	cap.open(index);
	start();
}

void VideoCaptureThread::stopCapture()
{
	m_isRunning = false;
	wait();
}

void VideoCaptureThread::run()
{
	m_isRunning = true;
	cv::Mat frame;
	QImage img;
	// 创建计时器并启动
    QElapsedTimer timer;
    timer.start();
    double fps = 0.0;
	while( true == m_isRunning && cap.isOpened() ){
		if (!cap.read(frame)) {
            std::cerr << "Frame read error" << std::endl;
            break;
        }
		// 计算处理上一帧所用的时间（单位：纳秒）
		qint64 elapsedTime = timer.nsecsElapsed();
		if (elapsedTime > 0) { 
			// 将纳秒转换成秒，并求倒数得到当前帧率
			fps = 1e9 / elapsedTime;
		}
		
		// 左上角显示帧率
		if(false == frame.empty()){
            cv::putText(frame, std::to_string(int(fps)), cv::Point(10, 30),
                        cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 0, 255), 2);
			img = QImage( frame.data,
						  static_cast<int>(frame.cols),
						  static_cast<int>(frame.rows),
						  static_cast<int>(frame.step),
						  QImage::Format_RGB888);
			if( m_isRunning != true ) break;
			// 加入缓存队列
			emit frameReady(img.rgbSwapped().copy());
		} else {
			std::cout << "frame is empty" << std::endl;
		}
		timer.restart();  // 重新启动计时器

		// 动态帧率控制
		if (fps > 0) {
			QThread::msleep(100 / fps);  // 等待指定的延迟时间
		}
	}
	frame.release();
}
