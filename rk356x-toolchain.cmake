# ================================================ #
# RK3568 Buildroot 交叉编译工具链配置
# 目标系统: Buildroot Linux (aarch64)
# 工具链路径: TOOLCHAIN_PATH (e.g /opt/atk-dlrk356x-toolchain)
# ================================================ #

# 核心编译器设置
set(CMAKE_TRY_COMPILE_TARGET_TYPE "STATIC_LIBRARY") # 避免尝试生成可执行文件
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSTEM_VERSION 1)

# 工具链路径 
if(NOT DEFINED TOOLCHAIN_PATH)
    message(STATUS "TOOLCHAIN_PATH not set in command line, checking environment...")
endif()

# 设置 Sysroot 
set(CMAKE_SYSROOT "${TOOLCHAIN_PATH}/aarch64-buildroot-linux-gnu/sysroot")

# 编译器路径
set(CMAKE_C_COMPILER ${TOOLCHAIN_PATH}/bin/aarch64-buildroot-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PATH}/bin/aarch64-buildroot-linux-gnu-g++)

message(STATUS "CMAKE_SYSROOT = ${CMAKE_SYSROOT}")
# 查找路径设置
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)     # 不在 sysroot 中找可执行程序
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)      # 只在 sysroot 中找库
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)      # 只在 sysroot 中找头文件
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)      # 只在 sysroot 中找包

# 强制编译器包含路径, 防止 CMake 忽略 SYSROOT
set(CMAKE_C_FLAGS "--sysroot=${CMAKE_SYSROOT}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "--sysroot=${CMAKE_SYSROOT}" CACHE STRING "" FORCE)

# CPU 架构优化标志 (针对 RK3568 的 Cortex-A55)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=cortex-a55 -mtune=cortex-a55" CACHE STRING "C Flags")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=cortex-a55 -mtune=cortex-a55" CACHE STRING "C++ Flags")