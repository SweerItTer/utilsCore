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
        this->registerHandler({ BTN_LEFT, BTN_RIGHT, BTN_SIDE, BTN_EXTRA }, [this, win, toCustomType](uint16_t btnType, uint8_t value) {
            int x = 0, y = 0;
            this->getPosition(x, y);
            Qt::MouseButton btn = Qt::NoButton;

            CustomMouseEvent::TypeId evType = toCustomType(value);
            switch (btnType) {
                case BTN_LEFT:
                    btn = Qt::LeftButton;
                    break;
                case BTN_RIGHT:
                    btn = Qt::RightButton;
                    break;
                case BTN_SIDE:
                    btn = Qt::BackButton;        // 后侧键
                    break;
                case BTN_EXTRA:
                    btn = Qt::ForwardButton;     // 前侧键
                    break;
                default:
                    evType = CustomMouseEvent::CustomMouseMove;
                    break;
            }

            // 使用 invokeMethod 异步调用 MainWindow 的槽, 确保在 GUI 线程执行
            QMetaObject::invokeMethod(win, [win, x, y, evType, btn]() {
                // fprintf(stdout, "Position: (%u, %u)\n", x, y);
                auto* me = new CustomMouseEvent(evType, QPoint(x, y), btn);
                QCoreApplication::postEvent(win, me);
            }, Qt::QueuedConnection);
        });
    }
};
