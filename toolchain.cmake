# 设置工具链的根目录
set(TOOLCHAIN_DIR /opt/atk-dlrv1126-toolchain)

# 设置C和C++编译器
set(CMAKE_C_COMPILER ${TOOLCHAIN_DIR}/usr/bin/arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_DIR}/usr/bin/arm-linux-gnueabihf-g++)

# 设置目标系统的名称和架构
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 设置工具链的sysroot，指向交叉编译环境的根目录
set(CMAKE_SYSROOT ${TOOLCHAIN_DIR}/arm-buildroot-linux-gnueabihf/sysroot)

# 设置查找头文件的路径
include_directories(${CMAKE_SYSROOT}/usr/include)

# 设置查找库文件的路径
link_directories(${CMAKE_SYSROOT}/usr/lib)

# 设置编译器的搜索路径，以找到必要的库和头文件
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
