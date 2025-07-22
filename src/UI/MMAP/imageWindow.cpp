/*
 * @FilePath: /EdgeVision/src/UI/MMAP/imageWindow.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-22 20:44:01
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "MMAP/imageWindow.h"

ImageWindow::ImageWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("Frame Viewer");
    resize(960, 540);  // 可按需调整

    imageLabel_ = new QLabel(this);
    imageLabel_->setAlignment(Qt::AlignCenter);
    imageLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(imageLabel_);
    setLayout(layout);
}

void ImageWindow::updateImage(const QImage& image)
{
    if (image.isNull()) return;
    imageLabel_->setPixmap(QPixmap::fromImage(image).scaled(
        imageLabel_->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation));
}