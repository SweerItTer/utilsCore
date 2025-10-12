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

// 渲染模式枚举
enum class RenderMode {
    KeepAspectRatio,  // 保持宽高比
    StretchToFill,    // 拉伸填充整个目标矩形
    CenterNoScale     // 居中显示
};

struct DrawBox {
    QRectF rect;
    QColor color;
    QString label;
    
    // 构造函数
    DrawBox() = default;
    DrawBox(const QRectF& r, const QColor& c, const QString& l) 
        : rect(r), color(c), label(l) {}
    DrawBox(qreal x, qreal y, qreal w, qreal h, const QColor& c, const QString& l)
        : rect(x, y, w, h), color(c), label(l) {}
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
    // 支持不同渲染模式
    void drawWidget(const Core::resourceSlot& slot, QWidget* widget, 
        const QRect& targetRect = QRect(),
        RenderMode mode = RenderMode::KeepAspectRatio);
private:
    std::unique_ptr<QOpenGLPaintDevice> device_;
    std::unique_ptr<QPainter> painter_;
    // 计算保持宽高比的矩形
    QRectF calculateAspectRatioRect(const QSize& sourceSize, const QRect& targetRect, const QSize& fboSize);
    // 计算拉伸填充的矩形
    QRectF calculateStretchRect(const QSize&, const QRect& targetRect, const QSize& fboSize);
    // 计算居中不缩放的矩形
    QRectF calculateCenterRect(const QSize& sourceSize, const QRect& targetRect, const QSize& fboSize);
    
    void bindFboAndPreparePainter(QOpenGLFramebufferObject* fbo);
};


#endif // DRAW_H
