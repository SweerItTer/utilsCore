/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-02 19:57:07
 * @FilePath: /EdgeVision/include/UI/MMAP/paramdialog.h
 */
#ifndef PARAM_DIALOG_H
#define PARAM_DIALOG_H

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QDialog>
#include <QCheckBox>
#include <QSpinBox>
#include <QLayoutItem>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMap>
#include <QCloseEvent>

#include "v4l2param/paramControl.h"

class ParamDialog : public QDialog {
    Q_OBJECT

public:
    explicit ParamDialog(QWidget* parent = nullptr);
    // 父对象(parent)销毁时自动析构
    
    // 载入一组控制信息 (创建控件组)
    void loadControls(const ParamControl::ControlInfos& controls);

    // 提取用户在 UI 中的设置
    ParamControl::ControlInfos getUserSettings() const;
signals:
    void configConfirmed();

private:
    void closeEvent(QCloseEvent* event);

    QVBoxLayout* layout_;  // 主布局
    // 保存旧数据
    ParamControl::ControlInfos originalControls_;
    // 用于保存 id 到控件的映射
    QMap<__u32, QWidget*> controlWidgets_;
};

#endif // PARAM_DIALOG_H
