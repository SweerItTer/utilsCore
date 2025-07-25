/*
 * @FilePath: /EdgeVision/src/UI/MMAP/mainwg.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-16 23:33:33
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "MMAP/mainwg.h"
#include "./ui_mainwg.h"

MainWg::MainWg(QWidget *parent)
	: QWidget(parent)
	, ui(new Ui::MainWg)
{
	ui->setupUi(this);
    
	// TestWindow = new ImageWindow();    
    
    DmaBuffer::initialize_drm_fd();

	// 初始化成员
	initVar();
	// 初始化信号
	initSignal();
}

MainWg::~MainWg()
{
    playThread->stopCapture();
    rgaThread->stop();
    cctr->stop();
    DmaBuffer::close_drm_fd();
	delete ui;
    // delete TestWindow;
}

void MainWg::initVar()
{
    int format 		= (V4L2_PIX_FMT_NV12 == cfg.format)
                 	? RK_FORMAT_YCbCr_420_SP 	// NV12
				 	: RK_FORMAT_YCrCb_422_SP; 	// MN16

	// 初始化队列
    rawFrameQueue  	= std::make_shared<FrameQueue>(20);
    frameQueue     	= std::make_shared<FrameQueue>(20);

    // 初始化线程
    cctr         	= std::make_shared<CameraController>(cfg);
	rgaThread 		= std::make_shared<RgaProcessor>(
                        cctr, rawFrameQueue, frameQueue,
                        cfg.width, cfg.height, 
                        format, RK_FORMAT_RGBA_8888,
                        10);
    playThread 		= std::make_unique<PlayThread>(
						nullptr, frameQueue, rgaThread, cfg.width, cfg.height);
    glwidget    	= std::make_unique<MyOpenGLWidget>(this);
	// 设置回调队列
    cctr->setFrameCallback([this](Frame f) {
        rawFrameQueue->enqueue(std::move(f));
    });
}

void MainWg::initSignal(){
	// QIamge 传递
	connect(playThread.get(), &PlayThread::frameReady, 
    [this](const QImage& img){
        // TestWindow->updateImage(img);

        glwidget->updateFrame(img);
    });
}

void MainWg::on_pushButton_clicked()
{
    static int status = 0;
    if (0 == status) {
        ui->pushButton->setText("Opened");
        status = 1;

        cctr->start();
        rgaThread->start();
        playThread->startCapture();  // 内部读取 frameQueue
        // TestWindow->show();  // 可放按钮点击中也行
    } else {
        ui->pushButton->setText("Closed");
        status = 0;
        
        playThread->stopCapture();
        rgaThread->stop();
        cctr->stop();
    }
}

