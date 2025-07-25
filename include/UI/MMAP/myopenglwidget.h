/*
 * @FilePath: /EdgeVision/include/UI/MMAP/myopenglwidget.h
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-07-16 23:34:00
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#ifndef MYOPENGLWIDGET_H
#define MYOPENGLWIDGET_H

#include <QImage>
#include <QMutex>
#include <QAtomicInteger>

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>

class MyOpenGLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit MyOpenGLWidget(QWidget *parent = nullptr);
    ~MyOpenGLWidget();

    // 槽函数
    void updateFrame(const QImage& frame);
    
protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    
private:
    void uploadTexture(const QImage& img);
    
private:
    QMutex mutex;
    QImage m_currentFrame;          // 当前帧
    QAtomicInteger<bool> textureReady; // 纹理更新标志
    
    // 顶点数据结构（位置 + 纹理坐标）
    struct Vertex {
        QVector3D position;
        QVector2D texCoord;
    };

    // OpenGL资源
    QOpenGLVertexArrayObject vao;   // 顶点数组对象
    QOpenGLBuffer vbo;              // 顶点缓冲对象
    QOpenGLShaderProgram program;   // 着色器程序
    GLuint texture;                 // 纹理对象
    QSize lastFrameSize;            // 上次纹理尺寸
};

#endif // MYOPENGLWIDGET_H