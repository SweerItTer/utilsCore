#include "myapplication.h"
#include "myopenglwiget.h"
#include "./ui_myapplication.h"

MyApplication::MyApplication(QWidget *parent)
	: QWidget(parent)
	, ui(new Ui::MyApplication)
{
	ui->setupUi(this);
	p_thread = std::make_shared<VideoCaptureThread>();

	p_thread->startCapture(0);

	connect(p_thread.get(), &VideoCaptureThread::frameReady, ui->widget, &MyOpenGLWiget::updataFrame);
}

MyApplication::~MyApplication()
{
	if(p_thread->isRunning())
		p_thread->stopCapture();
	delete ui;
}

