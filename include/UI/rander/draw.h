/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-17 21:03:29
 * @FilePath: /EdgeVision/include/UI/rander/draw.h
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef DRAW_H
#define DRAW_H

#include <QOpenGLPaintDevice>
#include <QOpenGLFramebufferObject>
#include <QPainter>
#include <QFont>
#include <QPen>
#include <QWidget>
#include <QString>
#include <QColor>
#include <QRectF>
#include <vector>

#include "rander/core.h"

struct DrawBox {
    QRectF rect;
    QColor color;
    QString label;
};

class Draw {
    Draw() {
        Core::instance().makeQCurrent();
        device_ = std::make_unique<QOpenGLPaintDevice>(QSize(1,1)); // 初始占位
        painter_ = std::make_unique<QPainter>(device_.get());
        painter_->setRenderHint(QPainter::Antialiasing);
        painter_->translate(0, 1); // 初始翻转占位
        painter_->scale(1, -1);
        Core::instance().doneQCurrent();
    }

public:
    static Draw& instance() {
        static Draw inst;
        return inst;
    }

    static void drawText(Core::resourceSlot& slot, const QString& text, const QPointF& pos,
                         const QColor& color = Qt::white, int fontSize = 24) 
    {
        auto& inst = instance();
        if (!slot.valid() || !slot.qfbo) return;

        Core::instance().makeQCurrent();
        QOpenGLFramebufferObject* fbo = slot.qfbo.get();
        if (!fbo->bind()) { Core::instance().doneQCurrent(); return; }

        inst.bindFboAndPreparePainter(fbo);

        QFont font = inst.painter_->font();
        font.setPointSize(fontSize);
        inst.painter_->setFont(font);
        inst.painter_->setPen(color);
        inst.painter_->drawText(pos, text);

        Core::instance().doneQCurrent();
    }

    static void clear(Core::resourceSlot& slot, const QColor& color = QColor(0,0,0,0)) {
        auto& inst = instance();
        if (!slot.valid() || !slot.qfbo) return;
    
        Core::instance().makeQCurrent();
        QOpenGLFramebufferObject* fbo = slot.qfbo.get();
        if (!fbo->bind()) { Core::instance().doneQCurrent(); return; }
    
        inst.bindFboAndPreparePainter(fbo);
        // 保持 alpha 通道
        inst.painter_->setCompositionMode(QPainter::CompositionMode_Source);
        inst.painter_->fillRect(QRectF(0,0,fbo->width(),fbo->height()), color);
        inst.painter_->setCompositionMode(QPainter::CompositionMode_SourceOver);
    
        Core::instance().doneQCurrent();
    }
    

    static void drawBoxes(Core::resourceSlot& slot, const std::vector<DrawBox>& boxes, int penWidth = 3) {
        auto& inst = instance();
        if (!slot.valid() || !slot.qfbo) return;

        Core::instance().makeQCurrent();
        QOpenGLFramebufferObject* fbo = slot.qfbo.get();
        if (!fbo->bind()) { Core::instance().doneQCurrent(); return; }

        inst.bindFboAndPreparePainter(fbo);

        for (const auto& box : boxes) {
            QPen pen(box.color);
            pen.setWidth(penWidth);
            inst.painter_->setPen(pen);
            inst.painter_->drawRect(box.rect);
            inst.painter_->drawText(box.rect.topLeft() + QPointF(2,20), box.label);
        }

        Core::instance().doneQCurrent();
    }

private:
    std::unique_ptr<QOpenGLPaintDevice> device_;
    std::unique_ptr<QPainter> painter_;

    void bindFboAndPreparePainter(QOpenGLFramebufferObject* fbo) {
        // 如果大小变化，更新 QOpenGLPaintDevice
        if (device_->size() != fbo->size()) {
            painter_->end();
            device_ = std::make_unique<QOpenGLPaintDevice>(fbo->size());
            painter_ = std::make_unique<QPainter>(device_.get());
            painter_->setRenderHint(QPainter::Antialiasing);
        }
        // 翻转坐标系
        painter_->resetTransform();
        painter_->translate(0, fbo->height());
        painter_->scale(1, -1);
    }
};


#endif // DRAW_H
