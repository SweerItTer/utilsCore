/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-08 15:37:24
 * @FilePath: /EdgeVision/src/UI/ConfigInterface/maininterface.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "ConfigInterface/maininterface.h"
#include "./ui_maininterface.h"
#include "qMouseWatch.h"
#include <QDebug>
#include <cmath>

double MainInterface::computeDPIScale(int sw, int sh) {
    // 基准屏幕尺寸 (1080p)
    constexpr double refW = 1280.0;
    constexpr double refH = 720.0;

    double screenPixels = static_cast<double>(sw) * static_cast<double>(sh);
    double refPixels = refW * refH;

    double scale = std::sqrt(screenPixels / refPixels);

    // 限制范围避免过度缩放
    if (scale < 0.5) scale = 0.5;
    if (scale > 3.0) scale = 3.0;

    return scale;
}

MainInterface::MainInterface(QWidget *parent)
    : QWidget(parent), ui(new Ui::MainInterface)
{
    ui->setupUi(this);
    this->move(0, 0);
    
    // 初始化原子变量
    visible_.store(false);
    confidence.store(50);  // 设置合理的默认值
    exposure.store(50);    // 曝光度默认值
    
    // 初始化模式状态
    captureMode_ = CaptureMode::Video;  // 默认录像模式
    recordingStatus_ = false;
    mirrorMode_ = MirrorMode::Normal;  // 默认水平镜像
    modelMode_  = ModelMode::Stop;
    
    this->setProperty("Visable", visible_.load());
    this->setProperty("ConfidenceValue", confidence.load());
    this->setProperty("ExposureValue", exposure.load());
    
    ui->exposureSlider->setValue(exposure.load());
    ui->confidenceSlider->setValue(confidence.load());
    ui->checkBox->setCheckState(Qt::Unchecked);
    
    // 初始化标签显示
    updateConfidenceLabel();
    updateExposureLabel();
    updateMirrorModeLabel();
    updateCaptureModeUI();
    
    registeSlot();
}

void MainInterface::registeSlot() {
    // 模式切换按钮 (录像/拍照)
    connect(ui->modeToggleButton, &QPushButton::pressed, [this] {
        if (captureMode_ == CaptureMode::Video) {
            captureMode_ = CaptureMode::Photo;
        } else {
            captureMode_ = CaptureMode::Video;
        }
        updateCaptureModeUI();
        emit captureModeChanged(captureMode_);
    });
    
    // 主动作按钮 (录制/拍照)
    connect(ui->actionButton, &QPushButton::pressed, [this] {
        if (captureMode_ == CaptureMode::Video) {
            // 录像模式
            qDebug() << "[MainInterface] Recording status: " << recordingStatus_;
            emit recordSignal(recordingStatus_);
            
            // 更新按钮文本
            if (recordingStatus_) {
                ui->actionButton->setText(tr("开始录制"));
            } else {
                ui->actionButton->setText(tr("结束录像"));
            }
            
            recordingStatus_ = !recordingStatus_;
        } else {
            // 拍照模式
            qDebug() << "[MainInterface] Taking photo";
            emit photoSignal();
        }
    });
    
    // 置信度控制
    connect(ui->confidenceADD, &QPushButton::pressed, [this] {
        if (confidence.load() >= ui->confidenceSlider->maximum()) return;
        confidence.fetch_add(1);
        ui->confidenceSlider->setValue(confidence.load());
        updateConfidenceLabel();
        emit confidenceChanged(confidence.load());
    });
    
    connect(ui->confidenceSUB, &QPushButton::pressed, [this] {
        if (confidence.load() <= ui->confidenceSlider->minimum()) return;
        confidence.fetch_sub(1);
        ui->confidenceSlider->setValue(confidence.load());
        updateConfidenceLabel();
        emit confidenceChanged(confidence.load());
    });
    
    connect(ui->confidenceSlider, &QSlider::valueChanged, [this](int value){
        confidence.store(value);
        updateConfidenceLabel();
        emit confidenceChanged(value);
    });
    
    // 曝光度控制
    connect(ui->exposureADD, &QPushButton::pressed, [this] {
        if (exposure.load() >= ui->exposureSlider->maximum()) return;
        exposure.fetch_add(1);
        ui->exposureSlider->setValue(exposure.load());
        updateExposureLabel();
        emit exposureChanged(exposure.load());
    });
    
    connect(ui->exposureSUB, &QPushButton::pressed, [this] {
        if (exposure.load() <= ui->exposureSlider->minimum()) return;
        exposure.fetch_sub(1);
        ui->exposureSlider->setValue(exposure.load());
        updateExposureLabel();
        emit exposureChanged(exposure.load());
    });
    
    connect(ui->exposureSlider, &QSlider::valueChanged, [this](int value){
        exposure.store(value);
        updateExposureLabel();
        emit exposureChanged(value);
    });
    
    // 镜像模式控制
    connect(ui->mirrorLeftButton, &QPushButton::pressed, [this] {
        cycleMirrorMode(false);  // 向左循环
    });
    
    connect(ui->mirrorRightButton, &QPushButton::pressed, [this] {
        cycleMirrorMode(true);   // 向右循环
    });
    connect(ui->checkBox, &QCheckBox::stateChanged, [this](int state){
        switch (state) {
        case Qt::Checked:
            modelMode_ = ModelMode::Run;
            break;
        case Qt::Unchecked:
            modelMode_ = ModelMode::Stop;
            break;
        default:
            break;
        }
        emit modelModeChange(modelMode_);
    });
}

MainInterface::~MainInterface() {
	delete ui;
}

void MainInterface::setUiDrawRect(const QRectF& r, qreal scale) {
    if (uiDrawRect_ == r && uiScale_ == scale) return; // 不变则返回
    uiDrawRect_ = r;
    if (scale <= 0.0){
        scale = 1.0;
    }
    uiScale_ = scale;
}

void MainInterface::updateFPS(float fps) {
    // 使用QMetaObject::invokeMethod确保线程安全
    QMetaObject::invokeMethod(this, [this, fps]() {
        ui->fpsShow->setText(QString("%1/s").arg(fps, 0, 'f', 1));
    }, Qt::QueuedConnection);
}

void MainInterface::updateCPUpayload(float payload) {
    // 使用QMetaObject::invokeMethod确保线程安全
    QMetaObject::invokeMethod(this, [this, payload]() {
        ui->CPUpayloadShow->setText(QString("%1%").arg(payload, 0, 'f', 1));
    }, Qt::QueuedConnection);
}

void MainInterface::updateMemoryUsage(float usage) {
    // 使用QMetaObject::invokeMethod确保线程安全
    QMetaObject::invokeMethod(this, [this, usage]() {
        ui->memoryUsageShow->setText(QString("%1%").arg(usage, 0, 'f', 1));
    }, Qt::QueuedConnection);
}

static QString getPerString(int max, int nowValue) {
	auto per = (double)nowValue / max * 100;
	auto per_s = QString::number((int)per) + QString("%");
	return per_s;
}

void MainInterface::updateConfidenceLabel(){
    auto value = this->confidence.load();
    this->setProperty("ConfidenceValue", value);
    ui->confidenceValueLabel->setText(getPerString(ui->confidenceSlider->maximum(), value));
}

void MainInterface::updateExposureLabel(){
    auto value = this->exposure.load();
    this->setProperty("ExposureValue", value);
    ui->exposureValueLabel->setText(getPerString(ui->exposureSlider->maximum(), value));
}

void MainInterface::updateMirrorModeLabel() {
    QString modeText;
    switch (mirrorMode_) {
        case MirrorMode::Normal:
            modeText = tr("标准");
            break;
        case MirrorMode::Horizontal:
            modeText = tr("水平镜像");
            break;
        case MirrorMode::Vertical:
            modeText = tr("垂直镜像");
            break;
        case MirrorMode::Both:
            modeText = tr("水平+垂直");
            break;
    }
    ui->mirrorModeLabel->setText(modeText);
}

void MainInterface::updateCaptureModeUI() {
    if (captureMode_ == CaptureMode::Video) {
        // 录像模式
        ui->modeToggleButton->setText(tr("录像"));
        if (recordingStatus_) {
            ui->actionButton->setText(tr("结束录像"));
        } else {
            ui->actionButton->setText(tr("开始录制"));
        }
    } else {
        // 拍照模式
        ui->modeToggleButton->setText(tr("拍照"));
        ui->actionButton->setText(tr("拍照"));
    }
}

void MainInterface::cycleMirrorMode(bool forward) {
    int currentMode = static_cast<int>(mirrorMode_);
    
    if (forward) {
        // 向右循环: 水平 -> 垂直 -> 水平+垂直 -> 水平
        currentMode = (currentMode + 1) % 4;
    } else {
        // 向左循环: 水平 -> 水平+垂直 -> 垂直 -> 水平
        currentMode = (currentMode - 1 + 4) % 4;
    }
    
    mirrorMode_ = static_cast<MirrorMode>(currentMode);
    updateMirrorModeLabel();
    emit mirrorModeChanged(mirrorMode_);
    
    qDebug() << "[MainInterface] Mirror mode changed to:" << static_cast<int>(mirrorMode_);
}

QPoint MainInterface::mapFromGlobal(const QPoint& pos) const {
    // 先把全局坐标移到 UI 实际绘制区域的原点, 再按缩放还原到 UI 逻辑坐标
    QPointF localF(
        (pos.x() - uiDrawRect_.x()) / uiScale_,
        (pos.y() - uiDrawRect_.y()) / uiScale_
    );

    return localF.toPoint(); // 向下取整与 Qt 行为一致
}

bool MainInterface::event(QEvent* e) {
    int t = static_cast<int>(e->type());
    if (t < CustomMouseEvent::CustomMouseMove ||
        t > CustomMouseEvent::CustomMouseRelease)
        return QWidget::event(e);

    auto* me = static_cast<CustomMouseEvent*>(e);

    // 右键显示/隐藏逻辑
    if (t == CustomMouseEvent::CustomMousePress) {
        if (me->button == Qt::RightButton || me->button == Qt::BackButton) {
            bool status = visible_.load();
            visible_.compare_exchange_weak(status, !status);
            this->setProperty("Visable", status);
            return true;
        }
    }

    QPoint globalPos = me->pos;

    // 把屏幕坐标变成 UI 内部坐标
    // 映射到 UI 的绘制区域
    QPoint uiPos = mapFromGlobal(globalPos);

    // 找目标控件
    QWidget* target = childAt(uiPos);
    if (nullptr == target)
        target = this;

    // 转为控件局部坐标
    QPoint targetLocal = target->mapFrom(this, uiPos);

    // 构造 Qt 鼠标事件
    QEvent::Type qtType =
        (t == CustomMouseEvent::CustomMousePress)   ? QEvent::MouseButtonPress  :
        (t == CustomMouseEvent::CustomMouseRelease) ? QEvent::MouseButtonRelease:
                                                     QEvent::MouseMove;

    QMouseEvent qme(
        qtType,
        targetLocal,     // 目标控件坐标
        uiPos,           // MainInterface 内坐标
        globalPos,       // 全局坐标
        me->button,
        me->button,
        Qt::NoModifier
    );
    // 派发事件给主线程
    return QApplication::sendEvent(target, &qme);
}