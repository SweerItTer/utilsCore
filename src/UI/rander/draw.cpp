/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-09-17 21:09:06
 * @FilePath: /EdgeVision/src/UI/rander/draw.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */

#include "rander/draw.h"
#include <QApplication>

Draw::Draw() {
    Core::instance().makeQCurrent();
    device_ = std::make_unique<QOpenGLPaintDevice>(QSize(1,1)); // 初始占位
    painter_ = std::make_unique<QPainter>(device_.get());
    painter_->setRenderHint(QPainter::Antialiasing);
    painter_->translate(0, 1); // 初始翻转占位
    painter_->scale(1, -1);
    Core::instance().doneQCurrent();
}

void Draw::drawWidget(const Core::resourceSlot& slot, QWidget* widget, 
                     const QRect& targetRect, RenderMode mode)
{
    if (nullptr == widget) {
        qWarning() << "Draw::drawWidget: null widget";
        return;
    }
    if (!slot.valid() || !slot.qfbo) return;

    Core::instance().makeQCurrent();
    QOpenGLFramebufferObject* fbo = slot.qfbo.get();
    if (!fbo->bind()) { Core::instance().doneQCurrent(); return; }

    bindFboAndPreparePainter(fbo);

    // 绘制属性
    painter_->setRenderHint(QPainter::Antialiasing, true);
    painter_->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter_->setRenderHint(QPainter::TextAntialiasing, true);

    // 计算绘制区域
    QRectF drawRect;
    switch (mode) {
        case RenderMode::KeepAspectRatio:
            drawRect = calculateAspectRatioRect(widget->size(), targetRect, fbo->size());
            break;
        case RenderMode::StretchToFill:
            drawRect = calculateStretchRect(widget->size(), targetRect, fbo->size());
            break;
        case RenderMode::CenterNoScale:
            drawRect = calculateCenterRect(widget->size(), targetRect, fbo->size());
            break;
    }

    if (drawRect.isNull()) {
        drawRect = QRectF(0, 0, fbo->width(), fbo->height());
    }

    // 🔥 使用 QPainter 变换缩放绘制
    painter_->save();
    {
        QSizeF srcSize = widget->size();
        qreal scaleX = drawRect.width() / srcSize.width();
        qreal scaleY = drawRect.height() / srcSize.height();

        painter_->translate(drawRect.x(), drawRect.y());
        painter_->scale(scaleX, scaleY);

        widget->render(painter_.get(), QPoint(0, 0), QRegion(), QWidget::DrawChildren);
    }
    painter_->restore();

    Core::instance().doneQCurrent();
}

QRectF Draw::calculateAspectRatioRect(const QSize& sourceSize, const QRect& targetRect, const QSize& fboSize)
{
    if (sourceSize.isEmpty() || targetRect.isEmpty()) {
        return QRectF(targetRect);
    }

    qreal srcW = sourceSize.width();
    qreal srcH = sourceSize.height();
    qreal dstW = targetRect.width();
    qreal dstH = targetRect.height();

    qreal scale = qMin(dstW / srcW, dstH / srcH);

    qreal newW = srcW * scale;
    qreal newH = srcH * scale;

    qreal x = targetRect.x() + (dstW - newW) / 2.0;
    qreal y = targetRect.y() + (dstH - newH) / 2.0;

    QRectF result(x, y, newW, newH);

    QRectF fboRect(0, 0, fboSize.width(), fboSize.height());
    return result.intersected(fboRect);
}

QRectF Draw::calculateStretchRect(const QSize&, const QRect& targetRect, const QSize& fboSize)
{
    QRectF fboRect(0, 0, fboSize.width(), fboSize.height());
    return QRectF(targetRect).intersected(fboRect);
}

QRectF Draw::calculateCenterRect(const QSize& sourceSize, const QRect& targetRect, const QSize& fboSize)
{
    if (sourceSize.isEmpty() || targetRect.isEmpty()) {
        return QRectF(targetRect);
    }

    qreal x = targetRect.x() + (targetRect.width() - sourceSize.width()) / 2.0;
    qreal y = targetRect.y() + (targetRect.height() - sourceSize.height()) / 2.0;

    QRectF result(x, y, sourceSize.width(), sourceSize.height());

    QRectF fboRect(0, 0, fboSize.width(), fboSize.height());
    return result.intersected(fboRect);
}

void Draw::drawText(const Core::resourceSlot &slot, const QString &text,
    const QPointF &pos, const QColor &color, int fontSize)
{
    auto& inst = instance();
    if (!slot.valid() || !slot.qfbo) return;

    Core::instance().makeQCurrent();
    QOpenGLFramebufferObject* fbo = slot.qfbo.get();
    if (!fbo->bind()) { Core::instance().doneQCurrent(); return; }

    bindFboAndPreparePainter(fbo);

    QFont font = painter_->font();
    font.setPointSize(fontSize);
    painter_->setFont(font);
    painter_->setPen(color);
    painter_->drawText(pos, text);

    Core::instance().doneQCurrent();
}

void Draw::drawBoxes(const Core::resourceSlot &slot, const std::vector<DrawBox> &boxes, int penWidth){
    if (!slot.valid() || !slot.qfbo) return;

    Core::instance().makeQCurrent();
    QOpenGLFramebufferObject* fbo = slot.qfbo.get();
    if (!fbo->bind()) { Core::instance().doneQCurrent(); return; }

    bindFboAndPreparePainter(fbo);

    for (const auto& box : boxes) {
        QPen pen(box.color);
        pen.setWidth(penWidth);
        painter_->setPen(pen);
        painter_->drawRect(box.rect);
        painter_->drawText(box.rect.topLeft() + QPointF(2,20), box.label);
    }

    Core::instance().doneQCurrent();
}

void Draw::clear(QOpenGLFramebufferObject * fbo, const QColor & color) {
    Core::instance().makeQCurrent();

    if (!fbo->bind()) { return; }

    bindFboAndPreparePainter(fbo);
    // 保持 alpha 通道
    painter_->setCompositionMode(QPainter::CompositionMode_Source);
    painter_->fillRect(QRectF(0,0,fbo->width(),fbo->height()), color);
    painter_->setCompositionMode(QPainter::CompositionMode_SourceOver);
    Core::instance().doneQCurrent();
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
}
