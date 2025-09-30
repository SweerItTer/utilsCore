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
#include <QDebug>

#include "rander/core.h"

struct DrawBox {
    QRectF rect;
    QColor color;
    QString label;
};

class Draw {
    Draw();    
public:
    void shutdown() {
        auto& core = Core::instance();
        core.makeQCurrent();   // 确保 OpenGL 上下文可用

        if (painter_ && painter_->isActive())
            painter_->end();   // 先结束绘制，避免析构访问 GL
        painter_.reset();
        device_.reset();

        core.doneQCurrent();   // 完成后释放上下文绑定
        fprintf(stdout, "[Draw] shutdown complete\n");
    }

    static Draw& instance() {
        static Draw inst;
        return inst;
    }
    // 手动清空画布
    void clear(QOpenGLFramebufferObject* fbo, const QColor& color = QColor(0,0,0,0));
    // 绘制文本
    void drawText(const Core::resourceSlot& slot, const QString& text, const QPointF& pos,
                         const QColor& color = Qt::white, int fontSize = 24);
    // 绘制多个框
    void drawBoxes(const Core::resourceSlot& slot, const std::vector<DrawBox>& boxes, int penWidth = 3);
    // 保持宽高比的widget渲染
    void drawWidget(const Core::resourceSlot& slot, QWidget* widget, 
        const QRect& targetRect = QRect());
private:
    std::unique_ptr<QOpenGLPaintDevice> device_;
    std::unique_ptr<QPainter> painter_;
    // 计算保持宽高比的矩形
    QRect calculateAspectRatioRect(const QSize& sourceSize, const QRect& targetRect, const QSize& fboSize);
    
    void bindFboAndPreparePainter(QOpenGLFramebufferObject* fbo);
};


#endif // DRAW_H
