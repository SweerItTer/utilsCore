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
    Logger::LogFlag = false;
	// 初始化成员
	initVar();
    
    // 主动更新可用参数(需要在paramProcessor_实例化后)
    auto allControls = paramProcessor_->param_.queryAllControls();
    paramDialog_ = new ParamDialog(this);
    if (nullptr != paramDialog_){
        paramDialog_->loadControls(allControls);
        paramDialog_->hide();
    }
    
	// 初始化信号
	initSignal();

    on_pushButton_clicked();
}

MainWg::~MainWg() {
    playThread->stopCapture();
    delete ui;
    delete paramDialog_;
    rgaThread->stop();
    cctr->stop();
    DmaBuffer::close_drm_fd();
}

void MainWg::initVar() {
    // 初始化队列
    rawFrameQueue  	= std::make_shared<FrameQueue>(20);
    frameQueue     	= std::make_shared<FrameQueue>(20);
    
	// 相机配置
    CameraController::Config cctr_cfg = {
        .buffer_count = 2,
        .plane_count = 2,
        .use_dmabuf = true,
        .device = "/dev/video0",
		// .width = 1920,
        // .height = 1080,

        .width = 3840,
        .height = 2160,
        .format = V4L2_PIX_FMT_NV12
    };

    Frame::MemoryType frameType = (true == cctr_cfg.use_dmabuf)
                    ? Frame::MemoryType::DMABUF
                    : Frame::MemoryType::MMAP;
    // 初始化线程
    cctr         	= std::make_shared<CameraController>(cctr_cfg);
    // 设置回调队列
    cctr->setFrameCallback([this](Frame f) {
        rawFrameQueue->enqueue(std::move(f));
    });
    
    // rga 配置 
    RgaProcessor::Config rga_cfg = {
        /* 这里需要延后配置,不然引用的都是旧数据,未初始化的空指针
         * 若任然想要实现提前配置结构体,延后初始化实例化对象,可以使用std::function
         * 但是意义不大,不是硬性要求
         */
        .cctr = cctr, 
        .rawQueue = rawFrameQueue,
        .outQueue = frameQueue,

        .width = cctr_cfg.width,
        .height = cctr_cfg.height,
        .frameType = frameType,
        .dstFormat = RK_FORMAT_RGBA_8888,
        .srcFormat = (V4L2_PIX_FMT_NV12 == cctr_cfg.format)
                     ? RK_FORMAT_YCbCr_420_SP
                     : RK_FORMAT_YCrCb_422_SP,
        .poolSize = 2
    };
	rgaThread 		= std::make_shared<RgaProcessor>(rga_cfg);
    
    // 这里我不知道这个玩意的意义(或许可以用来图像处理?)
    playThread 		= std::make_unique<PlayThread>(
						nullptr, frameQueue, rgaThread,
                        QSize(cctr_cfg.width, cctr_cfg.height));

    paramProcessor_ = std::make_unique<ParamProcessor>(cctr->getDeviceFd());
}

void MainWg::initSignal(){
	// void* 传递
	connect(playThread.get(), &PlayThread::frameReady, 
    [this](const void* data, const QSize& size, uint64_t timestamp,const int index){
        ui->openGLWidget->updateFrame(data, size, timestamp, index);
    });
    // dmabuf 传递
    connect(playThread.get(), &PlayThread::frameReadyDmabuf, 
    [this](const int fd, const QSize& size, uint64_t timestamp, const int index){
        ui->openGLWidget->updateFrameDmabuf(fd, size, timestamp, index);
    });
    // 回收资源
    connect(ui->openGLWidget, &MyOpenGLWidget::framedone, playThread.get(), &PlayThread::retuenBuff);
    // 显示配置界面
    connect(ui->openParamMenu, &QPushButton::clicked, [this](){
        paramDialog_->show();
    });
    // 控制参数更新
    connect(paramDialog_, &ParamDialog::configConfirmed, [this](){
        // 获取所有参数
        auto newSettings = paramDialog_->getUserSettings();
        // 更新参数
        paramProcessor_->setTargetControls(newSettings);
        paramDialog_->hide();
    });
}

void MainWg::on_pushButton_clicked(){
    static int status = 0;
    if (0 == status) {
        ui->pushButton->setText("Opened");
        status = 1;

        cctr->start();
        rgaThread->start();
        playThread->startCapture();  // 内部读取 frameQueue
        paramProcessor_->start();
    } else {
        ui->pushButton->setText("Closed");
        status = 0;
        
        playThread->pause();
        rgaThread->pause();
        cctr->pause();
        paramProcessor_->stop();
    }
}

