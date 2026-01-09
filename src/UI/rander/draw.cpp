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

// 核心辅助函数: 绑定原生FBO, 设置视口, 调整Painter
void Draw::bindFboAndPreparePainter(const Core::resourceSlot& slot) {
    // 1. 绑定原生 FBO (DMABUF 对应的 FBO)
    glBindFramebuffer(GL_FRAMEBUFFER, slot.blitFbo);

    // 2. 关键: 手动设置 Viewport！
    // QOpenGLFramebufferObject 会自动设置, 但原生 FBO 不会。必须设置, 否则可能画不出来。
    glViewport(0, 0, slot.width(), slot.height());

    // 3. 检查并调整 PaintDevice 大小
    QSize newSize(slot.width(), slot.height());
    if (device_->size() != newSize) {
        // 改变设备大小前必须结束 painter
        if (painter_->isActive()) {
            painter_->end();
        }
        device_->setSize(newSize);
    }

    // 4. 确保 Painter 处于激活状态
    if (!painter_->isActive()) {
        painter_->begin(device_.get());
        // 重置后需要重新设置 Hint, 因为 begin() 可能会重置状态
        painter_->setRenderHint(QPainter::Antialiasing);
    }

    // 5. 坐标系调整 (OpenGL原点在左下, Qt在左上)
    painter_->resetTransform();
    painter_->translate(0, slot.height());
    painter_->scale(1, -1);
}

DrawRect Draw::drawWidget(const Core::resourceSlot& slot, QWidget* widget, 
                     const QRectF& targetRect, RenderMode mode)
{
    DrawRect uiDrawRect;
    if (nullptr == widget) {
        qWarning() << "Draw::drawWidget: null widget";
        return uiDrawRect;
    }
    QVariant v = widget->property("Visible");
    if (!v.isValid() || v.toBool() == false) {
        return uiDrawRect;
    }
    if (!slot.valid()) return uiDrawRect;

    // [修改] 使用新的 helper 绑定
    bindFboAndPreparePainter(slot);

    // 绘制属性
    painter_->setRenderHint(QPainter::Antialiasing, true);
    painter_->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter_->setRenderHint(QPainter::TextAntialiasing, true);

    QSize fboSize(slot.width(), slot.height());

    // 计算绘制区域
    QRectF drawRect;
    switch (mode) {
        case RenderMode::KeepAspectRatio:
            drawRect = calculateAspectRatioRect(widget->size(), targetRect, fboSize);
            break;
        case RenderMode::StretchToFill:
            drawRect = calculateStretchRect(widget->size(), targetRect, fboSize);
            break;
        case RenderMode::CenterNoScale:
            drawRect = calculateCenterRect(widget->size(), targetRect, fboSize);
            break;
    }

    if (drawRect.isNull()) {
        drawRect = QRectF(0, 0, fboSize.width(), fboSize.height());
    }

    // 计算缩放比例
    qreal scaleX = drawRect.width() / widget->width();
    qreal scaleY = drawRect.height() / widget->height();
    qreal scale = std::min(scaleX, scaleY);

    QSize logicalSize(
        static_cast<int>(drawRect.width()  / scale),
        static_cast<int>(drawRect.height() / scale)
    );

    // 修复: 保持 widget 的 逻辑坐标系 和 绘制坐标系 完全一致
    if (widget->size() != logicalSize) {
        if (abs(widget->width() - logicalSize.width()) > 1 || 
            abs(widget->height() - logicalSize.height()) > 1) {
             widget->resize(logicalSize);
        }
    }

    // 绘制
    painter_->save();
    {
        painter_->translate(drawRect.x(), drawRect.y());
        painter_->scale(scale, scale);
        // 直接渲染到 Native FBO
        widget->render(painter_.get(), QPoint(0, 0), QRegion(), QWidget::DrawChildren);
    }
    painter_->restore();

    uiDrawRect.rect = drawRect;
    uiDrawRect.scale = scale;
    
    return uiDrawRect;
}

void Draw::drawText(const Core::resourceSlot &slot, const QString &text,
    const QPointF &pos, const QColor &color, int fontSize)
{
    if (!slot.valid()) return;

    // [修改] 绑定 DMABUF FBO
    bindFboAndPreparePainter(slot);

    QFont font = painter_->font();
    font.setPointSize(fontSize);
    painter_->setFont(font);
    painter_->setPen(color);
    painter_->drawText(pos, text);
}

void Draw::drawBoxes(const Core::resourceSlot &slot, const std::vector<DrawBox> &boxes, int penWidth){
    if (!slot.valid()) return;

    // [修改] 绑定 DMABUF FBO
    bindFboAndPreparePainter(slot);

    for (const auto& box : boxes) {
        QPen pen(box.color);
        pen.setWidth(penWidth);
        painter_->setPen(pen);
        painter_->drawRect(box.rect);
        painter_->drawText(box.rect.topLeft() + QPointF(2,20), box.label);
    }
}

void Draw::drawImage(const Core::resourceSlot& slot, const QImage& img, 
                     const QPoint& targetPoint, const int size)
{
    if (!slot.valid()) return;
    if (img.isNull()) return;

    // [修改] 绑定 DMABUF FBO
    bindFboAndPreparePainter(slot);

    painter_->setRenderHint(QPainter::Antialiasing, true);
    painter_->setRenderHint(QPainter::SmoothPixmapTransform, true);

    // 计算绘制区域
    QRectF drawRect;
    if (size > 0) {
        // 等比例缩放到指定size
        qreal scale = static_cast<qreal>(size) / qMax(img.width(), img.height());
        int scaledW = static_cast<int>(img.width() * scale);
        int scaledH = static_cast<int>(img.height() * scale);
        drawRect = QRectF(targetPoint.x(), targetPoint.y(), scaledW, scaledH);
    } else {
        // 原始大小
        drawRect = QRectF(targetPoint.x(), targetPoint.y(), img.width(), img.height());
    }

    // 绘制图像
    painter_->drawImage(drawRect, img);
}

void Draw::clear(const Core::resourceSlot& slot, const QColor& color) {
    if (!slot.valid()) return;

    // 1. 绑定原生 FBO (必须要先绑定, glClear 是对当前绑定的 FBO 生效的)
    glBindFramebuffer(GL_FRAMEBUFFER, slot.blitFbo);

    // 2. 设置清除颜色 (注意: OpenGL 使用 0.0 ~ 1.0 的浮点数)
    // 使用 QColor 的浮点转换函数非常方便
    glClearColor(color.redF(), color.greenF(), color.blueF(), color.alphaF());

    // 3. 执行清除操作
    // GL_COLOR_BUFFER_BIT 告诉硬件只清空颜色缓冲区
    glClear(GL_COLOR_BUFFER_BIT);
}

// 辅助计算函数保持原样...
QRectF Draw::calculateAspectRatioRect(const QSize& sourceSize, const QRectF& targetRect, const QSize& fboSize) {
    if (sourceSize.isEmpty() || targetRect.isEmpty()) return targetRect;
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

QRectF Draw::calculateStretchRect(const QSize&, const QRectF& targetRect, const QSize& fboSize) {
    QRectF fboRect(0, 0, fboSize.width(), fboSize.height());
    return targetRect.intersected(fboRect);
}

QRectF Draw::calculateCenterRect(const QSize& sourceSize, const QRectF& targetRect, const QSize& fboSize) {
    if (sourceSize.isEmpty() || targetRect.isEmpty()) return targetRect;
    qreal x = targetRect.x() + (targetRect.width() - sourceSize.width()) / 2.0;
    qreal y = targetRect.y() + (targetRect.height() - sourceSize.height()) / 2.0;
    QRectF result(x, y, sourceSize.width(), sourceSize.height());
    QRectF fboRect(0, 0, fboSize.width(), fboSize.height());
    return result.intersected(fboRect);
}