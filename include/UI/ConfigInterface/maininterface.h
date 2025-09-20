#ifndef MAININTERFACE_H
#define MAININTERFACE_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class MainInterface; }
QT_END_NAMESPACE

class MainInterface : public QWidget
{
	Q_OBJECT

public:
	MainInterface(QWidget *parent = nullptr);
	~MainInterface();

private slots:
	void on_confidenceSlider_valueChanged(int value);

	void on_brightnessSlider_valueChanged(int value);

	void on_contrastSlider_valueChanged(int value);

private:
	Ui::MainInterface *ui;
};
#endif // MAININTERFACE_H
