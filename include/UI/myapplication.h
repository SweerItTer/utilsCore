#ifndef MYAPPLICATION_H
#define MYAPPLICATION_H

#include <QWidget>
#include <memory>
#include "videocapturethread.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MyApplication; }
QT_END_NAMESPACE

class MyApplication : public QWidget
{
	Q_OBJECT

public:
	MyApplication(QWidget *parent = nullptr);
	~MyApplication();

private:
	Ui::MyApplication *ui;
	std::shared_ptr<VideoCaptureThread> p_thread;
};
#endif // MYAPPLICATION_H
