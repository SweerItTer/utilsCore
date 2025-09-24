/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-17 21:09:06
 * @FilePath: /EdgeVision/src/UI/rander/draw.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */

#include "rander/draw.h"

Draw::Draw() {
    Core::instance().makeQCurrent();
    device_ = std::make_unique<QOpenGLPaintDevice>(QSize(1,1)); // 初始占位
    painter_ = std::make_unique<QPainter>(device_.get());
    painter_->setRenderHint(QPainter::Antialiasing);
    painter_->translate(0, 1); // 初始翻转占位
    painter_->scale(1, -1);
    Core::instance().doneQCurrent();
}

void Draw::drawText(const Core::resourceSlot &slot, const QString &text,
    const QPointF &pos, const QColor &color, int fontSize)
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

void Draw::drawBoxes(const Core::resourceSlot &slot, const std::vector<DrawBox> &boxes, int penWidth){
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

void Draw::clear(QOpenGLFramebufferObject * fbo, const QColor & color){
    if (!fbo->bind()) { return; }

    // 保持 alpha 通道
    painter_->setCompositionMode(QPainter::CompositionMode_Source);
    painter_->fillRect(QRectF(0,0,fbo->width(),fbo->height()), color);
    painter_->setCompositionMode(QPainter::CompositionMode_SourceOver);
    // fbo->release();
}

void Draw::bindFboAndPreparePainter(QOpenGLFramebufferObject *fbo) {
    // 如果大小变化, 更新 QOpenGLPaintDevice
    if (device_->size() != fbo->size()) {
        painter_->end();
        device_->setSize(fbo->size());
        painter_->begin(device_.get());
        painter_->setRenderHint(QPainter::Antialiasing);
    }
    // 翻转坐标系
    painter_->resetTransform();
    painter_->translate(0, fbo->height());
    painter_->scale(1, -1);
    clear(fbo);
}
