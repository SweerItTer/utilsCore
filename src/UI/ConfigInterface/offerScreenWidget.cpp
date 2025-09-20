/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-15 02:13:41
 * @FilePath: /EdgeVision/src/UI/ConfigInterface/offerScreenWidget.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "ConfigInterface/offerScreenWidget.h"

OfferScreenWidget::OfferScreenWidget(QWidget *parent)
{
    mainInterface_ = std::unique_ptr<MainInterface>(new MainInterface());

    mainInterface_->show();
}
OfferScreenWidget::~OfferScreenWidget()
{
}