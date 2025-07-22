/*
 * @FilePath: /EdgeVision/include/UI/MMAP/imgwidget.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-22 20:43:50
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QImage>

class ImageWindow : public QWidget {
    Q_OBJECT
public:
    explicit ImageWindow(QWidget* parent = nullptr);

public slots:
    void updateImage(const QImage& image);

private:
    QLabel* imageLabel_;
};
