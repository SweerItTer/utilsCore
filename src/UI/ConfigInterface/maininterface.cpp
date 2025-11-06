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

MainInterface::MainInterface(QWidget *parent)
	: QWidget(parent)
	, ui(new Ui::MainInterface)
{
	ui->setupUi(this);

    connect(ui->startButton, &QPushButton::pressed, [](){
        qDebug() << "你好\n";
    });
    qDebug() << "当前坐标: " << this->pos();
    this->move(0, 0);
    qDebug() << "移动后坐标: " << this->pos();
}

MainInterface::~MainInterface() {
	delete ui;
}

void MainInterface::setUiDrawRect(const QRectF& r, qreal scale) {
    uiDrawRect_ = r;
    if (scale <= 0.0){
        scale = 1.0;
    }
    uiScale_ = scale;
}

static QString getPerString(int max, int nowValue) {
	auto per = (double)nowValue / max * 100;
	auto per_s = QString::number((int)per) + QString("%");
	return per_s;
}

void MainInterface::on_confidenceSlider_valueChanged(int value) {
	ui->confidenceValueLabel->setText(getPerString(ui->confidenceSlider->maximum(), value));
}


void MainInterface::on_brightnessSlider_valueChanged(int value) {
	ui->brightnessValueLabel->setText(getPerString(ui->brightnessSlider->maximum(), value));
}


void MainInterface::on_contrastSlider_valueChanged(int value) {
	ui->contrastValueLabel->setText(getPerString(ui->contrastSlider->maximum(), value));
}

QPoint MainInterface::mapFromGlobal(const QPoint& pos) const
{
    // 先把全局坐标移到 UI 实际绘制区域的原点, 再按缩放还原到 UI 逻辑坐标
    QPointF localF(
        (pos.x() - uiDrawRect_.x()) / uiScale_,
        (pos.y() - uiDrawRect_.y()) / uiScale_
    );

    return localF.toPoint(); // 向下取整与 Qt 行为一致
}

bool MainInterface::event(QEvent* e)
{
    int t = static_cast<int>(e->type());
    if (t < CustomMouseEvent::CustomMouseMove ||
        t > CustomMouseEvent::CustomMouseRelease)
        return QWidget::event(e);

    auto* me = static_cast<CustomMouseEvent*>(e);

    // 右键显示/隐藏逻辑（保持你原来的）
    if (t == CustomMouseEvent::CustomMousePress) {
        if (me->button == Qt::RightButton || me->button == Qt::BackButton) {
            this->isVisible() ? this->hide() : this->show();
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

    // Step6：派发事件
    return QApplication::sendEvent(target, &qme);
}
