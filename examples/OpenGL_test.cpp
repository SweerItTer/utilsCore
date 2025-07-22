/*
 * @FilePath: /EdgeVision/examples/OpenGL_test.cpp
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-05-21 19:21:51
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "MMAP/mainwg.h"

#include <QApplication>

int main(int argc, char *argv[])
{
	QApplication a(argc, argv);
	MainWg w;
	w.show();
	return a.exec();
}
