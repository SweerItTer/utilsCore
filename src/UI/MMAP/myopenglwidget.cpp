#include "MMAP/myopenglwidget.h"
#include <QDebug>
#include <QOpenGLShader>

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

    qDebug() << "OpenGL version:" << reinterpret_cast<const char*>(glGetString(GL_VERSION));
    qDebug() << "GLSL version:" << reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));

    // 顶点和纹理坐标数据
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

    // 创建初始占位纹理 (1x1 红色像素)
    unsigned char initData[] = {255, 0, 0, 255}; // RGBA
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, initData);

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
    }
}

void MyOpenGLWidget::updateFrame(const QImage &frame)
{
    // 如果上一帧尚未处理完成，跳过新帧
    if (!textureReady.loadAcquire()) {
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
    
    // 调试：保存第一帧用于验证
    static bool firstFrame = true;
    if (firstFrame) {
        if (m_currentFrame.save("/tmp/first_frame.png")) {
            qDebug() << "Saved first frame to /tmp/first_frame.png";
        }
        firstFrame = false;
    }
    
    // 触发绘制
    update();
}