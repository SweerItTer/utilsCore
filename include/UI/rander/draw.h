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

// 渲染模式枚举保持不变
enum class RenderMode {
    KeepAspectRatio,  
    StretchToFill,    
    CenterNoScale     
};

struct DrawRect {
    QRectF rect;
    qreal scale;
};

struct DrawBox {
    QRectF rect;
    QColor color;
    QString label;
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
        core.makeQCurrent();
        if (painter_ && painter_->isActive())
            painter_->end();
        painter_.reset();
        device_.reset();
        core.doneQCurrent();
        fprintf(stdout, "[Draw] shutdown complete\n");
    }

    static Draw& instance() {
        static Draw inst;
        return inst;
    }

    // [修改] 参数变更为 resourceSlot，不再依赖 qfbo 指针
    void clear(const Core::resourceSlot& slot, const QColor& color = QColor(0,0,0,0));
    
    void drawText(const Core::resourceSlot& slot, const QString& text, const QPointF& pos,
                    const QColor& color = Qt::white, int fontSize = 24);
    
    void drawImage(const Core::resourceSlot& slot, const QImage& img, 
                    const QPoint& targetPoint, const int size);
    
    void drawBoxes(const Core::resourceSlot& slot, const std::vector<DrawBox>& boxes, int penWidth = 3);
    
    DrawRect drawWidget(const Core::resourceSlot& slot, QWidget* widget, 
        const QRectF& targetRect = QRectF(),
        RenderMode mode = RenderMode::KeepAspectRatio);

private:
    std::unique_ptr<QOpenGLPaintDevice> device_;
    std::unique_ptr<QPainter> painter_;

    QRectF calculateAspectRatioRect(const QSize& sourceSize, const QRectF& targetRect, const QSize& fboSize);
    QRectF calculateStretchRect(const QSize&, const QRectF& targetRect, const QSize& fboSize);
    QRectF calculateCenterRect(const QSize& sourceSize, const QRectF& targetRect, const QSize& fboSize);
    
    // [修改] 传入 slot 以便获取原生 FBO ID 和尺寸
    void bindFboAndPreparePainter(const Core::resourceSlot& slot);
};

#endif // DRAW_H