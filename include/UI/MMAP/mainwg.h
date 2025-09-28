/*
 * @FilePath: /EdgeVision/include/UI/MMAP/mainwg.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-16 23:34:00
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef MAINWG_H
#define MAINWG_H

#include <QWidget>
#include <QLabel>
#include <QPixmap>

#include "v4l2param/paramProcessor.h"

#include "rga/rgaProcessor.h"
#include "MMAP/playthread.h"
#include "MMAP/myopenglwidget.h"
#include "MMAP/paramdialog.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWg; }
QT_END_NAMESPACE

class MainWg : public QWidget
{
	Q_OBJECT

public:
	MainWg(QWidget *parent = nullptr);
	~MainWg();

private slots:
	void on_pushButton_clicked();

private: /* -------- 私有成员函数 ------------- */
	void initVar();
	void initSignal();


private: /* -------- 私有成员变量 ------------- */
	// v4l2 MMAP 图像捕获线程
	std::shared_ptr<CameraController> cctr;
	// v4l2 视频参数设置
	std::unique_ptr<ParamProcessor> paramProcessor_;
	/* 两个安全队列
	 * v4l2 捕获后入队 rawFrameQueue 供 RGA 出队处理
	 * RGA 入队 frameQueue 处理后数据,供 PlayThread 出队传递给 MyOpenGLWidget 显示
	 */
	std::shared_ptr<FrameQueue> rawFrameQueue;
	std::shared_ptr<FrameQueue> frameQueue;
    // RGA 转换线程
	std::shared_ptr<RgaProcessor> rgaThread;
	// RGA 处理后图像捕获线程
	std::unique_ptr<PlayThread> playThread;
	// OpenGL 显示线程
	/* 别想了,哥们在这种简单的小问题上花了3天时间
	 * UI提升的时候会自动给你实例化了,直接调用ui->gl即可
	 */
	
	Ui::MainWg *ui;

	ParamDialog* paramDialog_ = nullptr;
};
#endif // MAINWG_H
