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
#include <QDateTime>
#include <cmath>

double MainInterface::computeDPIScale(int sw, int sh) {
    // 基准屏幕尺寸 (720p)
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
    visible_.store(true);
    confidence.store(50.0f);  // 设置默认值为50.0
    exposure.store(50.0f);    // 曝光度默认值为50.0
    
    // 初始化模式状态
    captureMode_ = CaptureMode::Video;  // 默认录像模式
    recordingStatus_ = false;
    mirrorMode_ = MirrorMode::Normal;  // 默认水平镜像
    modelMode_  = ModelMode::Stop;
    
    this->setProperty("Visible", visible_.load());
    this->setProperty("ConfidenceValue", confidence.load());
    this->setProperty("ExposureValue", exposure.load());
    
    // 从浮点数转换为滑块值
    ui->exposureSlider->setValue(floatToSlider(exposure.load(), ui->exposureSlider->maximum()));
    ui->confidenceSlider->setValue(floatToSlider(confidence.load(), ui->confidenceSlider->maximum()));
    ui->checkBox->setCheckState(Qt::Unchecked);
    
    // 初始化标签显示
    updateConfidenceLabel();
    updateExposureLabel();
    updateMirrorModeLabel();
    updateCaptureModeUI();
    
    // 初始化统一防抖定时器: 使用单个定时器管理所有滑块的延迟任务
    debounceTimer_ = new QTimer(this);
    debounceTimer_->setInterval(DEBOUNCE_DELAY_MS);
    debounceTimer_->setSingleShot(false);  // 持续运行, 每次检查待处理任务
    connect(debounceTimer_, &QTimer::timeout, this, &MainInterface::onDebounceTimeout);
    debounceTimer_->start();
    
    registeSlot();
}

float MainInterface::sliderToFloat(int sliderValue, int sliderMax) const {
    // 将滑块值(0-100)转换为浮点数(0.0-100.0)
    return (static_cast<float>(sliderValue) / sliderMax) * 100.0f;
}

int MainInterface::floatToSlider(float value, int sliderMax) const {
    // 将浮点数(0.0-100.0)转换为滑块值(0-100)
    return static_cast<int>((value / 100.0f) * sliderMax + 0.5f);  // 四舍五入
}

void MainInterface::debounceSlider(const QString& key, std::function<void()> callback) {
    // 记录任务和时间戳: 同一个key会覆盖旧任务, 实现自动去重
    DebounceTask task;
    task.callback = callback;
    task.timestamp = QDateTime::currentMSecsSinceEpoch();
    
    // 更新或插入任务(同一个key会覆盖旧任务)
    debounceTasks_[key] = task;
}

void MainInterface::onDebounceTimeout() {
    // 检查并执行已等待足够时间的防抖任务
    if (debounceTasks_.isEmpty()) return;
    
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    QStringList keysToRemove;
    
    // 遍历所有待处理任务
    for (auto it = debounceTasks_.begin(); it != debounceTasks_.end(); ++it) {
        qint64 elapsed = currentTime - it.value().timestamp;
        
        // 如果任务已经等待足够长时间(>=DEBOUNCE_DELAY_MS), 执行回调
        if (elapsed >= DEBOUNCE_DELAY_MS) {
            it.value().callback();  // 执行回调
            keysToRemove.append(it.key());
            qDebug() << "[MainInterface] Debounced task executed:" << it.key();
        }
    }
    
    // 移除已执行的任务
    for (const QString& key : keysToRemove) {
        debounceTasks_.remove(key);
    }
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
        if (captureMode_ == CaptureMode::Photo) {
            // 拍照模式
            qDebug() << "[MainInterface] Taking photo";
            emit photoSignal();
            return;
        }
        
        recordingStatus_ = !recordingStatus_;
        // 录像模式
        qDebug() << "[MainInterface] Recording status: " << recordingStatus_;
        emit recordSignal(recordingStatus_);
        
        // 更新按钮文本
        if (!recordingStatus_) {    // 状态为"未录像"
            ui->actionButton->setText(tr("开始录制"));
        } else {
            ui->actionButton->setText(tr("结束录像"));
        }
    });
    
    // 置信度控制 - 步进改为1.0 (按钮点击立即触发, 无需防抖)
    connect(ui->confidenceADD, &QPushButton::pressed, [this] {
        float currentValue = confidence.load();
        float maxValue = sliderToFloat(ui->confidenceSlider->maximum(), ui->confidenceSlider->maximum());
        
        if (currentValue >= maxValue) return;
        
        float newValue = currentValue + 1.0f;
        if (newValue > maxValue) newValue = maxValue;
        
        confidence.store(newValue);
        ui->confidenceSlider->setValue(floatToSlider(newValue, ui->confidenceSlider->maximum()));
        updateConfidenceLabel();
        emit confidenceChanged(newValue);  // 按钮点击立即触发, 不使用防抖
    });
    
    connect(ui->confidenceSUB, &QPushButton::pressed, [this] {
        float currentValue = confidence.load();
        float minValue = sliderToFloat(ui->confidenceSlider->minimum(), ui->confidenceSlider->maximum());
        
        if (currentValue <= minValue) return;
        
        float newValue = currentValue - 1.0f;
        if (newValue < minValue) newValue = minValue;
        
        confidence.store(newValue);
        ui->confidenceSlider->setValue(floatToSlider(newValue, ui->confidenceSlider->maximum()));
        updateConfidenceLabel();
        emit confidenceChanged(newValue);  // 按钮点击立即触发, 不使用防抖
    });
    
    // 置信度滑块 - 使用统一防抖机制: UI立即更新, 信号延迟触发
    connect(ui->confidenceSlider, &QSlider::valueChanged, [this](int value){
        float floatValue = sliderToFloat(value, ui->confidenceSlider->maximum());
        confidence.store(floatValue);
        updateConfidenceLabel();  // UI 立即更新, 保证界面响应流畅
        
        // 使用统一防抖方法: 延迟触发信号, 避免高频调用
        debounceSlider("confidence", [this, floatValue]() {
            emit confidenceChanged(floatValue);
        });
    });
    
    // 曝光度控制 - 步进改为1.0 (按钮点击立即触发, 无需防抖)
    connect(ui->exposureADD, &QPushButton::pressed, [this] {
        float currentValue = exposure.load();
        float maxValue = sliderToFloat(ui->exposureSlider->maximum(), ui->exposureSlider->maximum());
        
        if (currentValue >= maxValue) return;
        
        float newValue = currentValue + 1.0f;
        if (newValue > maxValue) newValue = maxValue;
        
        exposure.store(newValue);
        ui->exposureSlider->setValue(floatToSlider(newValue, ui->exposureSlider->maximum()));
        updateExposureLabel();
        emit exposureChanged(newValue);  // 按钮点击立即触发, 不使用防抖
    });
    
    connect(ui->exposureSUB, &QPushButton::pressed, [this] {
        float currentValue = exposure.load();
        float minValue = sliderToFloat(ui->exposureSlider->minimum(), ui->exposureSlider->maximum());
        
        if (currentValue <= minValue) return;
        
        float newValue = currentValue - 1.0f;
        if (newValue < minValue) newValue = minValue;
        
        exposure.store(newValue);
        ui->exposureSlider->setValue(floatToSlider(newValue, ui->exposureSlider->maximum()));
        updateExposureLabel();
        emit exposureChanged(newValue);  // 按钮点击立即触发, 不使用防抖
    });
    
    // 曝光度滑块 - 使用统一防抖机制: UI立即更新, 信号延迟触发
    connect(ui->exposureSlider, &QSlider::valueChanged, [this](int value){
        float floatValue = sliderToFloat(value, ui->exposureSlider->maximum());
        exposure.store(floatValue);
        updateExposureLabel();  // UI 立即更新, 保证界面响应流畅
        
        // 使用统一防抖方法: 延迟触发信号, 避免高频调用
        debounceSlider("exposure", [this, floatValue]() {
            emit exposureChanged(floatValue);
        });
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

void MainInterface::updateConfidenceLabel(){
    float value = this->confidence.load();
    this->setProperty("ConfidenceValue", value);
    // 显示为百分比, 保留一位小数
    ui->confidenceValueLabel->setText(QString("%1%").arg(value, 0, 'f', 1));
}

void MainInterface::updateExposureLabel(){
    float value = this->exposure.load();
    this->setProperty("ExposureValue", value);
    // 显示为百分比, 保留一位小数
    ui->exposureValueLabel->setText(QString("%1%").arg(value, 0, 'f', 1));
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
    QPointF p = mapFromGlobalF(pos);
    return QPoint(
        static_cast<int>(std::floor(p.x())),
        static_cast<int>(std::floor(p.y()))
    );
}

QPointF MainInterface::mapFromGlobalF(const QPoint& pos) const {
    // 精确反推 UI 逻辑坐标, 不做任何取整
    // qDebug() << "pos: ("<< pos.x() << ", " << pos.y() <<")";
    // qDebug() << "drawRect: ("<< uiDrawRect_.x() << ", " << uiDrawRect_.y() <<")";
    // qDebug() << "uiScale: "<< uiScale_;

    return QPointF(
        (pos.x() - uiDrawRect_.x()) / uiScale_,
        (pos.y() - uiDrawRect_.y()) / uiScale_
    );
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
            this->setProperty("Visible", status);
            return true;
        }
    }

    QPoint globalPos = me->pos;

    // 使用 高精度屏幕坐标 变成 UI 内部坐标    
    QPointF uiPosF = mapFromGlobalF(me->pos);
    // 命中判断仍用 int, 但只在最后一步
    QPoint uiPosI(
        static_cast<int>(std::floor(uiPosF.x())),
        static_cast<int>(std::floor(uiPosF.y()))
    );
    // qDebug() << "uiPosI: ("<< uiPosI.x() << ", " << uiPosI.y() <<")";

    // 找目标控件
    QWidget* target = childAt(uiPosI);
    if (nullptr == target){
        qDebug() << "No child at uiPosI";
        target = this;
    }
    QPointF targetLocalF = uiPosF;
    if (target != this) {
        // 将坐标映射到目标控件上 
        targetLocalF -= QPointF(target->x(), target->y());
    }
    // qDebug() << "uiPosF: ("<< uiPosF.x() << ", " << uiPosF.y() <<")";
    // qDebug() << "targetLocalF: ("<< targetLocalF.x() << ", " << targetLocalF.y() <<")";
    // qDebug() << "==============";

    // 构造 Qt 鼠标事件
    QEvent::Type qtType =
        (t == CustomMouseEvent::CustomMousePress)   ? QEvent::MouseButtonPress  :
        (t == CustomMouseEvent::CustomMouseRelease) ? QEvent::MouseButtonRelease:
                                                     QEvent::MouseMove;

    QMouseEvent qme(
        qtType,
        targetLocalF,     // 目标控件坐标
        uiPosI,           // MainInterface 内坐标
        globalPos,        // 全局坐标
        me->button,
        me->button,
        Qt::NoModifier
    );
    // 派发事件给主线程
    return QApplication::sendEvent(target, &qme);
}