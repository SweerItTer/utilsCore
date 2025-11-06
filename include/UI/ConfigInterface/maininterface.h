/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-30 17:38:49
 * @FilePath: /EdgeVision/include/UI/ConfigInterface/maininterface.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef MAININTERFACE_H
#define MAININTERFACE_H

#include <QWidget>
#include <QMouseEvent>

QT_BEGIN_NAMESPACE
namespace Ui { class MainInterface; }
QT_END_NAMESPACE

class MainInterface : public QWidget
{
	Q_OBJECT

public:
	MainInterface(QWidget *parent = nullptr);
	~MainInterface();
	
	// 用于更新 UI 实际绘制区域
	void setUiDrawRect(const QRectF& r, qreal scale);
private slots:
	void on_confidenceSlider_valueChanged(int value);

	void on_brightnessSlider_valueChanged(int value);

	void on_contrastSlider_valueChanged(int value);

protected:
    bool event(QEvent *e) override;

	// 坐标映射：全局坐标 → UI本地坐标
	QPoint mapFromGlobal(const QPoint &pos) const;
private:
	QRectF uiDrawRect_;		// 离屏渲染中 UI 真实显示的位置
	qreal uiScale_{1.0};   	// 缩放倍率 
	Ui::MainInterface *ui;
};
#endif // MAININTERFACE_H
