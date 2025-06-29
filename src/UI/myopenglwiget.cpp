#include "myopenglwiget.h"
#include <QDebug>

MyOpenGLWiget::MyOpenGLWiget(QWidget *parent) 
	: QOpenGLWidget(parent), vbo(QOpenGLBuffer::VertexBuffer) // 明确指定为顶点缓冲区
{

}

MyOpenGLWiget::~MyOpenGLWiget()
{
    makeCurrent(); 					// 激活当前OpenGL上下文
    
    vbo.destroy();					// 销毁顶点缓冲区
    vao.destroy(); 					// 销毁顶点数组对象			
    glDeleteTextures(1, &texture); 	// 删除纹理
    
    doneCurrent(); 					// 释放上下文
}

void MyOpenGLWiget::initializeGL()
{
	// 初始化OpenGL
	initializeOpenGLFunctions();
	    // ===== 1. 准备顶点数据 =====
    // 定义四边形顶点数据（两个三角形组成）
    Vertex vertices[] = {
        // 第一个三角形（左下部分）
        {{-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f}}, // 左下角，纹理坐标(0,1)
        {{ 1.0f, -1.0f, 0.0f}, {1.0f, 1.0f}}, // 右下角，纹理坐标(1,1)
        {{-1.0f,  1.0f, 0.0f}, {0.0f, 0.0f}}, // 左上角，纹理坐标(0,0)
        
        // 第二个三角形（右上部分）
        {{ 1.0f, -1.0f, 0.0f}, {1.0f, 1.0f}}, // 右下角
        {{ 1.0f,  1.0f, 0.0f}, {1.0f, 0.0f}}, // 右上角，纹理坐标(1,0)
        {{-1.0f,  1.0f, 0.0f}, {0.0f, 0.0f}}  // 左上角
    };

    // ===== 2. 配置VAO/VBO =====
    vao.create(); // 创建顶点数组对象
    vao.bind();   // 绑定VAO（后续操作将记录到VAO）

    vbo.create(); // 创建顶点缓冲区
    vbo.bind();   // 绑定VBO
    vbo.allocate(vertices, sizeof(vertices)); // 上传数据到显存

    // ================== 3. 初始化着色器 ==================
    program.addCacheableShaderFromSourceFile(	//顶点着色器
        QOpenGLShader::Vertex, "./res/texture.vert");
    program.addCacheableShaderFromSourceFile(	//片段着色器
        QOpenGLShader::Fragment, "./res/texture.frag");
    
    if (!program.link()) {
        qCritical() << "Shader link error:" << program.log();
    }

    // ===== 4. 配置顶点属性 =====
    program.bind(); 					// 绑定着色器程序
    
    // 位置属性（location 0） (x,y,z)
    program.enableAttributeArray(0); 	// 启用属性通道0
    program.setAttributeBuffer(      	// 配置数据格式
        0,                            	// 属性位置
        GL_FLOAT,                      	// 数据类型
        offsetof(Vertex, position),    	// 数据偏移量
        3,                             	// 向量分量数（vec3）
        sizeof(Vertex)                 	// 步长（单个顶点数据总大小）
    );
    
    // 纹理坐标属性（location 1）(u,v) 图像坐标
    program.enableAttributeArray(1);
    program.setAttributeBuffer(
        1,
        GL_FLOAT,
        offsetof(Vertex, texCoord),
        2,                             	// vec2类型
        sizeof(Vertex)
    );

    // ===== 5. 初始化纹理 =====
    glGenTextures(1, &texture);      		// 生成纹理对象
    glBindTexture(GL_TEXTURE_2D, texture); 	// 绑定2D纹理
    
    // 设置纹理参数
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);	// 纹理坐标超出范围时的处理方式
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);		// 纹理缩小过滤方式
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);		// 纹理放大过滤方式
    
    // 初始化空纹理（黑色）
    glTexImage2D(
        GL_TEXTURE_2D,    // 目标
        0,                // mipmap层级
        GL_RGB8,          // 内部格式
        640, 480,         // 宽高（初始尺寸）
        0,                // 必须为0
        GL_RGB,           // 数据格式
        GL_UNSIGNED_BYTE, // 数据类型
        nullptr           // 初始数据（空）
    );

    // ===== 6. 清理状态 =====
    program.release(); // 解绑着色器
    vao.release();     // 解绑VAO
}

void MyOpenGLWiget::resizeGL(int w, int h)
{
	glViewport(0, 0, w, h);
}

void MyOpenGLWiget::paintGL()
{
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // 清屏颜色（黑色）
    glClear(GL_COLOR_BUFFER_BIT);          // 执行清屏

    // ===== 1. 更新纹理 =====
    {
        QMutexLocker locker(&mutex); // 线程安全锁
        
        if (!m_currentFrame.isNull()) {
            glBindTexture(GL_TEXTURE_2D, texture);
            
            // 更新纹理数据（仅当有新帧时）
            glTexSubImage2D(
                GL_TEXTURE_2D,       // 目标
                0,                    // mipmap层级
                0, 0,                 // 偏移量
                m_currentFrame.width(),  // 图像宽度
                m_currentFrame.height(), // 图像高度
                GL_RGB,              // 数据格式
                GL_UNSIGNED_BYTE,    // 数据类型
                m_currentFrame.bits() // 像素数据指针
            );
        }
    }


    // ================== 2. 执行渲染 ==================
    program.bind();
    glBindTexture(GL_TEXTURE_2D, texture);
    vao.bind();
	// 绘制
    glDrawArrays(GL_TRIANGLES, 0, 6);

    vao.release();
    program.release();
}

void MyOpenGLWiget::updataFrame(const QImage &frame)
{
	QMutexLocker locker(&mutex);
	m_currentFrame = frame.convertToFormat(QImage::Format_RGB888);
	// qDebug() << "Get Frame.";
	update();
}
