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

void Draw::drawWidget(const Core::resourceSlot& slot, QWidget* widget, const QRect& targetRect)
{
    if (!widget) {
        qWarning() << "Draw::drawWidget: null widget";
        return;
    }
    auto& inst = instance();
    if (!slot.valid() || !slot.qfbo) return;

    Core::instance().makeQCurrent();
    QOpenGLFramebufferObject* fbo = slot.qfbo.get();
    if (!fbo->bind()) { Core::instance().doneQCurrent(); return; }

    bindFboAndPreparePainter(fbo);

    // 设置抗锯齿等绘制属性
    painter_->setRenderHint(QPainter::Antialiasing, true);
    painter_->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter_->setRenderHint(QPainter::TextAntialiasing, true);

    // 计算保持宽高比的目标矩形
    QRect drawRect = calculateAspectRatioRect(widget->size(), targetRect, fbo->size());
    
    if (drawRect.isNull()) {
        drawRect = QRect(0, 0, fbo->width(), fbo->height());
    }

    // 使用 render() 方法渲染整个widget到计算好的矩形中
    widget->render(painter_.get(), drawRect.topLeft(), QRegion(), QWidget::DrawChildren);

    // 结束绘制
    Core::instance().doneQCurrent();

    // qDebug() << "Widget rendered to FBO, original:" << widget->size() 
    //          << "target:" << targetRect << "actual:" << drawRect;
}

QRect Draw::calculateAspectRatioRect(const QSize& sourceSize, const QRect& targetRect, const QSize& fboSize)
{
    if (sourceSize.isEmpty() || targetRect.isEmpty()) {
        return targetRect;
    }

    // 计算源和目标的宽高比
    float sourceAspect = (float)sourceSize.width() / sourceSize.height();
    float targetAspect = (float)targetRect.width() / targetRect.height();
    
    QRect resultRect = targetRect;
    
    if (sourceAspect > targetAspect) {
        // 源更宽，按宽度适配，高度自动计算
        int newHeight = targetRect.width() / sourceAspect;
        int yOffset = (targetRect.height() - newHeight) / 2;
        resultRect = QRect(targetRect.x(), targetRect.y() + yOffset, 
                          targetRect.width(), newHeight);
    } else {
        // 源更高，按高度适配，宽度自动计算
        int newWidth = targetRect.height() * sourceAspect;
        int xOffset = (targetRect.width() - newWidth) / 2;
        resultRect = QRect(targetRect.x() + xOffset, targetRect.y(), 
                          newWidth, targetRect.height());
    }
    
    // 确保不超出 FBO 边界
    return resultRect.intersected(QRect(0, 0, fboSize.width(), fboSize.height()));
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
