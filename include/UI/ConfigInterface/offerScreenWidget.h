/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-15 02:11:59
 * @FilePath: /EdgeVision/include/UI/ConfigInterface/offerScreenWidget.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef OFFERSCREENWIDGET_H
#define OFFERSCREENWIDGET_H

#include <memory>
#include <QWidget>

#include "maininterface.h"

class MainInterface;

class OfferScreenWidget : public QWidget
{
    Q_OBJECT

public:
    explicit OfferScreenWidget(QWidget *parent = nullptr);
    ~OfferScreenWidget();

private:
    std::unique_ptr<MainInterface> mainInterface_;
};

#endif // OFFERSCREENWIDGET_H