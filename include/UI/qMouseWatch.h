/*
* @Author: SweerItTer xxxzhou.xian@gmail.com
* @Date: 2025-11-04 18:45:54
 * @FilePath: /EdgeVision/include/UI/qMouseWatch.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
*/

#pragma once
#include <QCoreApplication>
#include <QWidget>
#include <QEvent>
#include <QPoint>
#include <QMouseEvent>
#include <QDebug>

#include "mouse/watcher.h"

class CustomMouseEvent : public QEvent {
public:
    enum TypeId { CustomMouseMove = QEvent::User + 1, CustomMousePress, CustomMouseRelease };
    QPoint pos;
    Qt::MouseButton button;

    CustomMouseEvent(TypeId type, const QPoint& p, Qt::MouseButton b = Qt::NoButton)
        : QEvent(static_cast<QEvent::Type>(type)), pos(p), button(b) {}
};

class QMouseWatch : public MouseWatcher {
public:
    QMouseWatch() = default;
    ~QMouseWatch() = default;

    void setNotifyWindow(QWidget* win) {
        auto toCustomType = [](uint8_t value) -> CustomMouseEvent::TypeId {
            return (value == 1) ? CustomMouseEvent::CustomMousePress
                                : CustomMouseEvent::CustomMouseRelease;
        };

        // 注册左右键事件回调
        this->registerHandler(
            { MouseEventType::ButtonLeft, MouseEventType::ButtonRight, MouseEventType::ButtonSide, MouseEventType::ButtonExtra },
            [this, win, toCustomType] (MouseEventType btnType, uint8_t value) {
            int x = 0, y = 0;
            if (!this->getMappedPosition(x, y)) return;
            Qt::MouseButton btn = Qt::NoButton;

            CustomMouseEvent::TypeId evType = toCustomType(value);
            switch (btnType) {
                case MouseEventType::ButtonLeft:
                    btn = Qt::LeftButton;
                    break;
                case MouseEventType::ButtonRight:
                    btn = Qt::RightButton;
                    break;
                case MouseEventType::ButtonSide:
                    btn = Qt::BackButton;        // 后侧键
                    break;
                case MouseEventType::ButtonExtra:
                    btn = Qt::ForwardButton;     // 前侧键
                    break;
                default:
                    evType = CustomMouseEvent::CustomMouseMove;
                    break;
            }

            // 使用 invokeMethod 异步调用 MainWindow 的槽, 确保在 GUI 线程执行
            QMetaObject::invokeMethod(win, [win, x, y, evType, btn]() {
                auto* me = new CustomMouseEvent(evType, QPoint(x, y), btn);
                QCoreApplication::postEvent(win, me);
            }, Qt::QueuedConnection);
        });
    }
};
