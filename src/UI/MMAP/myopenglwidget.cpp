#include "MMAP/myopenglwidget.h"
#include <QDebug>
#include <QOpenGLShader>
#include <QElapsedTimer>
#include <unistd.h>

extern "C" {
#include <drm/drm_fourcc.h>
}

#include "v4l2/frame.h"
#include "logger.h"

MyOpenGLWidget::MyOpenGLWidget(QWidget *parent)
    : QOpenGLWidget(parent), vbo(QOpenGLBuffer::VertexBuffer),
      texture(0), textureReady(true)
{
    // 使用 OpenGL ES 3.2 适用于 rk3568
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGLES);
    format.setVersion(3, 2);
    format.setProfile(QSurfaceFormat::CoreProfile);
    setFormat(format);
}

MyOpenGLWidget::~MyOpenGLWidget()
{
    makeCurrent();

    vbo.destroy();
    vao.destroy();

    if (texture != 0) {
        glDeleteTextures(1, &texture);
        texture = 0;
    }

    if (currentEGLImage != EGL_NO_IMAGE_KHR) {
        eglDestroyImageKHR(eglGetCurrentDisplay(), currentEGLImage);
        currentEGLImage = EGL_NO_IMAGE_KHR;
    }
    
    doneCurrent();
    fprintf(stdout, "MyOpenGLWidget::~MyOpenGLWidget()\n");
}

void MyOpenGLWidget::initializeGL()
{
    initializeOpenGLFunctions();

    // 输出调试信息
    qDebug() << "\033[0m\033[1;33mOpenGL version:\033[0m" << reinterpret_cast<const char*>(glGetString(GL_VERSION)) ;
    qDebug() << "\033[0m\033[1;33mGLSL version:\033[0m" << reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    qDebug() << "\033[0m\033[1;33mRenderer:\033[0m" << reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    qDebug() << "\033[0m\033[1;33mVendor:\033[0m" << reinterpret_cast<const char*>(glGetString(GL_VENDOR));

    // 加载扩展函数指针
    eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");

    if ((nullptr == eglCreateImageKHR) || (nullptr == eglDestroyImageKHR) || (nullptr == glEGLImageTargetTexture2DOES)) {
        qCritical() << "EGL extensions not available";
    }


    // 三角形坐标级纹理坐标
    Vertex vertices[] = {
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
        {{ 1.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-1.0f,  1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 1.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 1.0f,  1.0f, 0.0f}, {1.0f, 0.0f}},
        {{-1.0f,  1.0f, 0.0f}, {0.0f, 0.0f}},
    };

    vao.create();
    vao.bind();

    vbo.create();
    vbo.bind();
    vbo.allocate(vertices, sizeof(vertices));

    // 顶点着色器 - 使用 #version 300 es
    const char* vertCode = R"(#version 300 es
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec2 aTexCoord;
        out vec2 TexCoord;
        void main() {
            gl_Position = vec4(aPos, 1.0);
            TexCoord = aTexCoord;
        })";

    // 片段着色器 - 使用 #version 300 es
    const char* fragCode = R"(#version 300 es
        precision mediump float;
        in vec2 TexCoord;
        out vec4 FragColor;
        uniform sampler2D ourTexture;
        void main() {
            FragColor = texture(ourTexture, TexCoord);
        })";

    program.addCacheableShaderFromSourceCode(QOpenGLShader::Vertex, vertCode);
    program.addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, fragCode);
    if (!program.link()) {
        qCritical() << "Shader link error:" << program.log();
        // 输出详细的着色器错误信息
        if (program.shaders().count() > 0) {
            QOpenGLShader *vshader = program.shaders().at(0);
            QOpenGLShader *fshader = program.shaders().at(1);
            qCritical() << "Vertex shader log:" << vshader->log();
            qCritical() << "Fragment shader log:" << fshader->log();
        }
    }

    program.bind();

    program.enableAttributeArray(0);
    program.setAttributeBuffer(0, GL_FLOAT, offsetof(Vertex, position), 3, sizeof(Vertex));

    program.enableAttributeArray(1);
    program.setAttributeBuffer(1, GL_FLOAT, offsetof(Vertex, texCoord), 2, sizeof(Vertex));

    // 初始化纹理对象
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    // 设置默认纹理参数
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // 分配初始纹理内存
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1920, 1080, 0, 
                GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    vao.release();
    program.release();

    // 检查OpenGL错误
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        qWarning() << "OpenGL error after initialization:" << err;
    }
}

void MyOpenGLWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void MyOpenGLWidget::paintGL()
{
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    QMutexLocker locker(&mutex);

    // 仅当有新帧时渲染
    if (currentFrameType == NONE) {
        return;
    }

    program.bind();
    vao.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    program.setUniformValue("ourTexture", 0);
    bool frameProcessed = false;

    auto t4 = mk::timeDiffMs(dequeueTimestamp, "[tex upload]");

    // 处理MMAP帧
    if (currentFrameType == MMAP && currentFrameData) {
        uploadTexture(currentFrameData, currentFrameSize);
        frameProcessed = true;
    }
    // 处理DMABUF帧
    else if (currentFrameType == DMABUF && currentDmabufFd >= 0) {
        if (importDmabufToTexture(currentDmabufFd, currentFrameSize)) {
            frameProcessed = true;
        } else {
            qWarning() << "Failed to import DMABUF frame";
        }
    }

    if (frameProcessed) {
        glDrawArrays(GL_TRIANGLES, 0, 6);
        // 通知 playthread 回收
        emit framedone(currentFrameIndex);
    }

    mk::timeDiffMs(t4, "[render]");
    Logger::log(stdout, "===\n");
    
    vao.release();
    program.release();

    // 重置状态
    currentFrameType = NONE;
    currentFrameData = nullptr;
    currentDmabufFd = -1;
    textureReady.storeRelease(true);

    // 强制刷新
    glFlush();
}

void MyOpenGLWidget::uploadTexture(const void* data, const QSize& size)
{
    if (!data || size.isEmpty()) return;

    glBindTexture(GL_TEXTURE_2D, texture);

    // 计算字节对齐
    int bytesPerLine = size.width() * 4;
    int alignment = 1;
    if ((bytesPerLine % 8) == 0) alignment = 8;
    else if ((bytesPerLine % 4) == 0) alignment = 4;
    else if ((bytesPerLine % 2) == 0) alignment = 2;
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);

    // 尺寸变化时重新分配纹理
    if (size != lastFrameSize) {
        lastFrameSize = size;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 
                    size.width(), size.height(), 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, data);
    } 
    // 尺寸相同时更新纹理
    else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                       size.width(), size.height(),
                       GL_RGBA, GL_UNSIGNED_BYTE, data);
    }

    // 检查错误
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        qWarning() << "Texture upload error:" << err;
    }
}

void MyOpenGLWidget::updateFrame(const void* data, const QSize& size, uint64_t timestamp, const int index)
{
    // 检查纹理是否就绪
    if (!textureReady.testAndSetAcquire(true, false)) {
        qDebug() << "Skipped frame: texture busy";
        emit framedone(index);
        return;
    }

    // 更新帧数据
    dequeueTimestamp = timestamp;
    currentFrameType = MMAP;
    currentFrameData = data;
    currentFrameSize = size;
    currentFrameIndex = index;

    // 请求重绘
    update();
}

void MyOpenGLWidget::updateFrameDmabuf(int fd, const QSize& size, uint64_t timestamp, int index)
{
    // 检查纹理是否就绪
    if (!textureReady.testAndSetAcquire(true, false)) {
        qDebug() << "Skipped DMABUF frame: texture busy";
        emit framedone(index);
        return;
    }

    // 更新帧数据
    dequeueTimestamp = timestamp;
    currentFrameType = DMABUF;
    currentDmabufFd = fd;
    currentFrameSize = size;
    currentFrameIndex = index;

    // 请求重绘
    update();
}

bool MyOpenGLWidget::importDmabufToTexture(int fd, const QSize& size)
{
    EGLDisplay eglDisplay = eglGetCurrentDisplay();
    if (eglDisplay == EGL_NO_DISPLAY) {
        qWarning() << "Invalid EGL display";
        return false;
    }

    if (QOpenGLContext::currentContext() == nullptr) {
        qWarning() << "No OpenGL context";
        return false;
    }

    // 设置EGLImage属性
    const EGLint attribs[] = {
        EGL_WIDTH, size.width(),
        EGL_HEIGHT, size.height(),
        EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_ABGR8888,
        EGL_DMA_BUF_PLANE0_FD_EXT, fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT, size.width() * 4,
        EGL_NONE
    };

    if (EGL_NO_IMAGE_KHR != currentEGLImage) {
        eglDestroyImageKHR(eglDisplay, currentEGLImage);
        currentEGLImage = EGL_NO_IMAGE_KHR;
    }    

    // 创建EGLImage
    currentEGLImage = eglCreateImageKHR(
        eglDisplay,
        EGL_NO_CONTEXT,
        EGL_LINUX_DMA_BUF_EXT,
        nullptr,
        attribs
    );

    if (currentEGLImage == EGL_NO_IMAGE_KHR) {
        qWarning() << "Failed to create EGLImage:" << eglGetError();
        return false;
    }

    // 绑定到纹理
    glBindTexture(GL_TEXTURE_2D, texture);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, currentEGLImage);

    // 更新纹理尺寸
    if (lastFrameSize != size) {
        lastFrameSize = size;
    }

    // 清理资源
    eglDestroyImageKHR(eglDisplay, currentEGLImage);

    // 检查错误
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        qWarning() << "OpenGL error:" << err;
        return false;
    }

    return true;
}