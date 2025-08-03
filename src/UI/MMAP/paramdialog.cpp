/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-08-02 19:58:30
 * @FilePath: /EdgeVision/src/UI/MMAP/paramdialog.cpp
 */
#include "MMAP/paramdialog.h"

ParamDialog::ParamDialog(QWidget* parent)
    : QDialog(parent)
{
    this->setWindowTitle("参数设置");
    this->setMinimumWidth(400);

    layout_ = new QVBoxLayout(this);
    this->setLayout(layout_);

    // 应用配置按钮
    QPushButton* applyButton = new QPushButton("应用配置");

    // 居中放置按钮
    QHBoxLayout* buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    buttonLayout->addWidget(applyButton);
    buttonLayout->addStretch();

    // 添加到底部
    layout_->addLayout(buttonLayout);

    // 点击按钮时发出配置确认信号
    connect(applyButton, &QPushButton::clicked, this, [this]() {
        emit configConfirmed();
    });
}

void ParamDialog::closeEvent(QCloseEvent* event)
{
    // close调用不会析构
    // 恢复控件状态为初始状态
    loadControls(originalControls_);
    QDialog::closeEvent(event);  // 继续执行默认关闭
}


void ParamDialog::loadControls(const ParamControl::ControlInfos& controls)
{
    // 清空旧控件
    QLayoutItem* child;
    while ((child = layout_->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    controlWidgets_.clear();

    // 控件宽度统一
    const int labelWidth = 120;

    for (const auto& info : controls) {
        // 主要调整部分
        QWidget* container = new QWidget;
        QHBoxLayout* hLayout = new QHBoxLayout(container);
        hLayout->setContentsMargins(0, 0, 0, 0);

        // 名称标签部分
        QLabel* label = new QLabel(QString::fromStdString(info.name));
        label->setFixedWidth(labelWidth);
        hLayout->addWidget(label);

        // 基类指针指向子类对象 QSpinBox/QCheckBox 是 QWidget 的子类
        QWidget* control = nullptr;

        // 状态开关
        if (ParamControl::isSwitchControl(info)) {
            QCheckBox* checkbox = new QCheckBox;
            checkbox->setChecked(info.current != 0);
            control = checkbox;
            container->setProperty("checkbox_ptr", QVariant::fromValue(static_cast<void*>(checkbox)));

        // 数值调节
        } else if (ParamControl::isValueControl(info)) {

            QSpinBox* spinbox = new QSpinBox;
            // 设置滑条范围
            spinbox->setRange(info.min, info.max);
            // 设置步长
            spinbox->setSingleStep(info.step);
            // 显示当前值
            spinbox->setValue(info.current);
            control = spinbox;
            // 使用 void* 保存
            container->setProperty("spinbox_ptr", QVariant::fromValue(static_cast<void*>(spinbox)));
        }

        if (nullptr != control) {
            hLayout->addWidget(control);
            hLayout->addStretch(); // 保证控件靠左对齐
            container->setProperty("control_id", static_cast<int>(info.id));

            layout_->addWidget(container);
            // 保存映射关系 (id和对应的控件容器)
            controlWidgets_.insert(info.id, container);
        } else {
            delete container;  // 控件无效时清除
        }
    }

    layout_->addStretch();
    // 备份配置
    originalControls_ = controls;
}

ParamControl::ControlInfos ParamDialog::getUserSettings() const
{
    ParamControl::ControlInfos result;

    for (auto it = controlWidgets_.constBegin(); it != controlWidgets_.constEnd(); ++it) {
        V4L2ControlInfo info;
        info.id = it.key(); // 从 map key 获取 ID
        QWidget* container = it.value(); // 存储的是 container QWidget*

        // 获取 checkbox 控件状态
        if (container->property("checkbox_ptr").isValid()) {
            void* ptr = container->property("checkbox_ptr").value<void*>();
            if (auto* checkbox = static_cast<QCheckBox*>(ptr)) {
                info.current = checkbox->isChecked() ? 1 : 0;
            }

        // 获取 spinbox 控件数值
        } else if (container->property("spinbox_ptr").isValid()) {
            void* ptr = container->property("spinbox_ptr").value<void*>();
            if (auto* spinbox = static_cast<QSpinBox*>(ptr)) {
                info.current = spinbox->value();
            }
        }

        // 通过label获取name
        if (auto* layout = container->layout()) {
            if (auto* label = qobject_cast<QLabel*>(layout->itemAt(0)->widget())) {
                info.name = label->text().toStdString();
            }
        }
        
        result.emplace_back(info);
    }

    return result;
}
