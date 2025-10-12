/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-10-08 15:37:24
 * @FilePath: /EdgeVision/src/UI/ConfigInterface/maininterface.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "ConfigInterface/maininterface.h"
#include "./ui_maininterface.h"
#include <QDebug>

MainInterface::MainInterface(QWidget *parent)
	: QWidget(parent)
	, ui(new Ui::MainInterface)
{
	ui->setupUi(this);
}

MainInterface::~MainInterface()
{
	delete ui;
}

static QString getPerString(int max, int nowValue){
	auto per = (double)nowValue / max * 100;
	auto per_s = QString::number((int)per) + QString("%");
	return per_s;
}

void MainInterface::on_confidenceSlider_valueChanged(int value)
{
	ui->confidenceValueLabel->setText(getPerString(ui->confidenceSlider->maximum(), value));
}


void MainInterface::on_brightnessSlider_valueChanged(int value)
{
	ui->brightnessValueLabel->setText(getPerString(ui->brightnessSlider->maximum(), value));
}


void MainInterface::on_contrastSlider_valueChanged(int value)
{
	ui->contrastValueLabel->setText(getPerString(ui->contrastSlider->maximum(), value));
}

