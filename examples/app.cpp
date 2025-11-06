/*
 * @Author: SweerItTer xxxzhou.xian@gmail.com
 * @Date: 2025-11-06 11:46:07
 * @FilePath: /EdgeVision/examples/app.cpp
 * @LastEditors: SweerItTer xxxzhou.xian@gmail.com
 */
#include "EGui.h"

int main(int argc, char *argv[]) {
	DrmDev::fd_ptr = DeviceController::create(); // 初始化全局唯一fd_ptr
	
	FrameBufferTest test;
	test.start();
	test.RunUI(argc, argv);
	return 0;
}
