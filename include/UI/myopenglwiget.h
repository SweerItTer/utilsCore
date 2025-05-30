#ifndef MYOPENGLWIGET_H
#define MYOPENGLWIGET_H

#include <QImage>
#include <QMutex>

#include <QOpenGLFunctions>

#include <QOpenGLWidget>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>

class MyOpenGLWiget : public QOpenGLWidget, private QOpenGLFunctions
{
	Q_OBJECT

public:
	explicit MyOpenGLWiget(QWidget *parent = nullptr);
	~MyOpenGLWiget();

	// 槽函数
	void updataFrame(const QImage& frame);
protected:
	void initializeGL() override;
	void resizeGL(int w, int h) override;
	void paintGL() override;
private:
	// 线程安全
	QMutex mutex;
	QImage m_currentFrame;			// 当前帧

    // 顶点数据结构（位置 + 纹理坐标）
    struct Vertex {
        QVector3D position;
        QVector2D texCoord;
    };

	// 资源
	QOpenGLVertexArrayObject vao;	// 顶点数组对象
	QOpenGLBuffer vbo;				// 顶点缓冲对象
	QOpenGLShaderProgram program;	// 着色器程序
	GLuint texture;					// 纹理对象

};

#endif // MYOPENGLWIGET_H
