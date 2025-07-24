#include "MMAP/myopenglwidget.h"
#include <QDebug>
#include <QOpenGLShader>
#include <QImage>

MyOpenGLWidget::MyOpenGLWidget(QWidget *parent)
    : QOpenGLWidget(parent), vbo(QOpenGLBuffer::VertexBuffer),
      textureReady(true)
{
    // 使用 OpenGL ES 3.2，适配 RK3568 平台
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

    doneCurrent();
}

void MyOpenGLWidget::initializeGL()
{
    initializeOpenGLFunctions();

    qDebug() << "\033[0m\033[1;33mOpenGL version:\033[0m" << reinterpret_cast<const char*>(glGetString(GL_VERSION)) ;
    qDebug() << "\033[0m\033[1;33mGLSL version:\033[0m" << reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    qDebug() << "\033[0m\033[1;33mRenderer:\033[0m" << reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    qDebug() << "\033[0m\033[1;33mVendor:\033[0m" << reinterpret_cast<const char*>(glGetString(GL_VENDOR));

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

    // 生成纹理
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    // 纹理参数设置 - 使用CLAMP_TO_EDGE避免边缘问题
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // 初始化 checkerboard 测试图
    QImage image(1920, 1080, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if ((x / 40 + y / 30) % 2 == 0)
                image.setPixelColor(x, y, Qt::red);
            else
                image.setPixelColor(x, y, Qt::yellow);
        }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width(), image.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());

    vao.release();
    program.release();

    // 检查OpenGL错误
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        qWarning() << "OpenGL error after initialization:" << err;
    }

    m_currentFrame = image;        // 赋值当前帧
    textureReady.storeRelease(true);
    update();                     // 触发绘制
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

    if (m_currentFrame.isNull()) {
        qDebug() << "No frame to render";
        return;
    }

    // 上传纹理
    uploadTexture(m_currentFrame);

    // 渲染纹理
    program.bind();
    vao.bind();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    program.setUniformValue("ourTexture", 0);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    vao.release();
    program.release();

    // 标记纹理操作完成
    textureReady.storeRelease(true);

    // 强制刷新
    glFlush();
}

void MyOpenGLWidget::uploadTexture(const QImage& img)
{
    // 确保纹理绑定
    glBindTexture(GL_TEXTURE_2D, texture);

    // 处理字节对齐 - RK3568平台需要特别注意
    int bpl = img.bytesPerLine();
    int bpp = img.depth() / 8;

    // 计算对齐方式
    GLint alignment = 1;
    if ((bpl % 8) == 0) alignment = 8;
    else if ((bpl % 4) == 0) alignment = 4;
    else if ((bpl % 2) == 0) alignment = 2;

    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);

    // 检查是否需要重新分配纹理内存
    if (img.size() != lastFrameSize) {
        lastFrameSize = img.size();

        // 分配新的纹理内存
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,  // 使用明确的内部格式
            img.width(),
            img.height(),
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            img.constBits()
        );

        qDebug() << "Texture reallocated:" << img.size();
    } else {
        // 更新现有纹理
        glTexSubImage2D(
            GL_TEXTURE_2D,
            0,
            0, 0,
            img.width(),
            img.height(),
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            img.constBits()
        );
    }

    // 检查OpenGL错误
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        qWarning() << "Texture upload error:" << err;
    } else qDebug() << "upload successed.";
}

void MyOpenGLWidget::updateFrame(const QImage &frame)
{
    if (false == textureReady.loadAcquire()) {
        qDebug() << "Skipped frame: texture busy";
        return;
    }

    QMutexLocker locker(&mutex);

    // 标记纹理操作开始
    textureReady.storeRelease(false);

    // 确保使用RGBA8888格式
    if (frame.format() != QImage::Format_RGBA8888) {
        m_currentFrame = frame.convertToFormat(QImage::Format_RGBA8888);
    } else {
        m_currentFrame = frame;
    }

    update();
}