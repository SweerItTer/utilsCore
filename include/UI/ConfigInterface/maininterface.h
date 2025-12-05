/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-30 17:38:49
 * @FilePath: /EdgeVision/include/UI/ConfigInterface/maininterface.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef MAININTERFACE_H
#define MAININTERFACE_H

#include <QWidget>
#include <atomic>
#include <QRectF>
#include <QMouseEvent>

QT_BEGIN_NAMESPACE
namespace Ui { class MainInterface; }
QT_END_NAMESPACE

class MainInterface : public QWidget {
	Q_OBJECT
	Ui::MainInterface *ui;
public:
	// 捕获模式枚举
	enum class CaptureMode {
		Video = 0,  // 录像模式
		Photo = 1   // 拍照模式
	};

	// 镜像模式枚举
	enum class MirrorMode {
        Normal = 0,      // 标准模式(无镜像)
        Horizontal = 1,  // 水平镜像
        Vertical = 2,    // 垂直镜像
        Both = 3         // 水平+垂直镜像
    };
	// 模型推理模式枚举
	enum class ModelMode {
        Run = 0,      // 运行推理
        Stop = 1	  // 关闭推理
    };

	MainInterface(QWidget *parent = nullptr);
	~MainInterface();
	// DPI缩放因子计算
	static double computeDPIScale(int sw, int sh);
	// 用于更新 UI 实际绘制区域
	void setUiDrawRect(const QRectF& r, qreal scale);

signals:
    void recordSignal(bool status);       // 录像信号
    void photoSignal();                   // 拍照信号
    void confidenceChanged(int value);    // 置信度改变
    void exposureChanged(int value);      // 曝光度改变
    void captureModeChanged(CaptureMode mode);  // 捕获模式改变
    void mirrorModeChanged(MirrorMode mode);    // 镜像模式改变
	void modelModeChange(ModelMode mode);	    // 模型开启状态
public slots:
	// 更新帧率
	void updateFPS(const float fps);
	// 更新CPU负载
	void updateCPUpayload(const float payload);
	// 更新内存使用状态
	void updateMemoryUsage(float usage); 

protected:
    bool event(QEvent *e) override;
private:
    void registeSlot();
    void updateConfidenceLabel();
    void updateExposureLabel();         // 更新曝光度标签
    void updateMirrorModeLabel();       // 更新镜像模式标签
    void updateCaptureModeUI();         // 更新捕获模式UI
    void cycleMirrorMode(bool forward); // 循环切换镜像模式
	// 全局坐标 -> UI本地坐标
	QPoint mapFromGlobal(const QPoint &pos) const;
private:
	QRectF uiDrawRect_;		// 离屏渲染中 UI 真实显示的位置
	qreal uiScale_{1.0};   	// 缩放倍率

	// 状态变量
	std::atomic_bool visible_{false};
	std::atomic_int confidence{0};
	std::atomic_int exposure{0};

	bool recordingStatus_ = false;      // 录像状态
    CaptureMode captureMode_;           // 捕获模式 (录像/拍照)
    MirrorMode mirrorMode_;             // 镜像模式
	ModelMode modelMode_;				// 模型推理模式
};

#endif // MAININTERFACE_H
