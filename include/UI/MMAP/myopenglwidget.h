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

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <QOpenGLContext>
#define EGL_EGLEXT_PROTOTYPES

class MyOpenGLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit MyOpenGLWidget(QWidget *parent = nullptr);
    ~MyOpenGLWidget();

    // 槽函数
    void updateFrame(const void* data, const QSize& size, const int index);
    void updateFrameDmabuf(const int fd, const QSize& size, const int index);
    
protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    
private:
    void uploadTexture(const void* data, const QSize& size);
    bool importDmabufToTexture(int fd, const QSize& size);
signals:
    // 缓冲区返回由 playthread 管理
	void framedone(const int index); // 请求返回数据

private:
    enum FrameType { NONE, MMAP, DMABUF };

    QMutex mutex;
    QAtomicInteger<bool> textureReady; // 纹理可更新标志

    union {
    /* 所有资源 fd, ptr都由外界管理
     * fd -> rgathread -> std::shared_ptr<DmaBuffer>
     * ptr -> rgathread
     */
        const void* currentFrameData; // mmap模式下裸指针
        int currentDmabufFd ;   // dmabuf模式下文件描述符
    };
    
    EGLImageKHR currentEGLImage = EGL_NO_IMAGE_KHR;

    FrameType currentFrameType = NONE;// 标志位 - 帧类型
    QSize currentFrameSize; // 帧宽高
    int currentFrameIndex = -1; // 用于释放内存池的序号标志
    
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

    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = nullptr;

};

#endif // MYOPENGLWIDGET_H