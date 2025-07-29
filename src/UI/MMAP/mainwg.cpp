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
    
    DmaBuffer::initialize_drm_fd();

	// 初始化成员
	initVar();
	// 初始化信号
	initSignal();

    everythingisdone = true;
}

MainWg::~MainWg()
{
    playThread->stopCapture();
    rgaThread->stop();
    cctr->stop();
    DmaBuffer::close_drm_fd();
	delete ui;
}

void MainWg::initVar()
{
	// 相机配置
    CameraController::Config cctr_cfg = {
        .buffer_count = 4,
        .plane_count = 2,
        .use_dmabuf = true,
        .device = "/dev/video0",
		.width = 1920,
        .height = 1080,

        // .width = 3840,
        // .height = 2160,
        .format = V4L2_PIX_FMT_NV12
    };

    int format 		= (V4L2_PIX_FMT_NV12 == cctr_cfg.format)
                 	? RK_FORMAT_YCbCr_420_SP 	// NV12
				 	: RK_FORMAT_YCrCb_422_SP; 	// MN16
    Frame::MemoryType frameType = (true == cctr_cfg.use_dmabuf)
                    ? Frame::MemoryType::DMABUF
                    : Frame::MemoryType::MMAP;
   	
	// rga 配置 
	RgaProcessor::Config rga_cfg = {
		.cctr = cctr, 
		.rawQueue = rawFrameQueue,
		.outQueue = frameQueue,
		.width = cctr_cfg.width,
		.height = cctr_cfg.height,
		.frameType = frameType,
		.dstFormat = format, 
		.srcFormat = RK_FORMAT_RGBA_8888,
		.poolSize = 10
	};

	// 初始化队列
    rawFrameQueue  	= std::make_shared<FrameQueue>(20);
    frameQueue     	= std::make_shared<FrameQueue>(20);

    // 初始化线程
    cctr         	= std::make_shared<CameraController>(cctr_cfg);
    // 设置回调队列
    cctr->setFrameCallback([this](Frame f) {
        rawFrameQueue->enqueue(std::move(f));
    });

	rgaThread 		= std::make_shared<RgaProcessor>(rga_cfg);
    // 这里我不知道这个玩意的意义(或许可以用来图像处理?)
    playThread 		= std::make_unique<PlayThread>(
						nullptr, frameQueue, rgaThread,
                        QSize(cctr_cfg.width, cctr_cfg.height));
}

void MainWg::initSignal(){
	// QIamge 传递
	connect(playThread.get(), &PlayThread::frameReady, 
    [this](const void* data, const QSize& size, const int index){
        ui->openGLWidget->updateFrame(data, size, index);
    });

    connect(playThread.get(), &PlayThread::frameReadyDmabuf, 
    [this](const int fd, const QSize& size, const int index){
        ui->openGLWidget->updateFrameDmabuf(fd, size, index);
    });

    connect(ui->openGLWidget, &MyOpenGLWidget::framedone, playThread.get(), &PlayThread::retuenBuff);
}

void MainWg::on_pushButton_clicked()
{
    if (false == everythingisdone) return;
    static int status = 0;
    if (0 == status) {
        ui->pushButton->setText("Opened");
        status = 1;

        cctr->start();
        rgaThread->start();
        playThread->startCapture();  // 内部读取 frameQueue
    } else {
        ui->pushButton->setText("Closed");
        status = 0;
        
        playThread->stopCapture();
        rgaThread->stop();
        cctr->pause();
    }
}

