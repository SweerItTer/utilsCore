# ================================================ #
# RK3568 Buildroot 交叉编译工具链配置
# 目标系统：Buildroot Linux (aarch64)
# 工具链路径：/opt/atk-dlrk356x-toolchain
# ================================================ #

# 核心编译器设置
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSTEM_VERSION 1)

# 工具链路径 
set(TOOLCHAIN_DIR /home/mouj/rk3568/buildroot/output/rockchip_rk3568/host)

# 编译器路径
set(CMAKE_C_COMPILER ${TOOLCHAIN_DIR}/usr/bin/aarch64-buildroot-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_DIR}/usr/bin/aarch64-buildroot-linux-gnu-g++)

# 设置 Sysroot 
set(CMAKE_SYSROOT ${TOOLCHAIN_DIR}/aarch64-buildroot-linux-gnu/sysroot)

# 查找路径设置
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)     # 不在 sysroot 中找可执行程序
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)      # 只在 sysroot 中找库
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)      # 只在 sysroot 中找头文件
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)      # 只在 sysroot 中找包

# CPU 架构优化标志 (针对 RK3568 的 Cortex-A55)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=cortex-a55 -mtune=cortex-a55" CACHE STRING "C Flags")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=cortex-a55 -mtune=cortex-a55" CACHE STRING "C++ Flags")

# # 其他工具设置
# set(CMAKE_AR ${TOOLCHAIN_DIR}/usr/bin/aarch64-buildroot-linux-gnu-ar)
# set(CMAKE_RANLIB ${TOOLCHAIN_DIR}/usr/bin/aarch64-buildroot-linux-gnu-ranlib)
# set(CMAKE_STRIP ${TOOLCHAIN_DIR}/usr/bin/aarch64-buildroot-linux-gnu-strip)
# set(CMAKE_NM ${TOOLCHAIN_DIR}/usr/bin/aarch64-buildroot-linux-gnu-nm)

# # 可选：pkg-config 配置
# set(ENV{PKG_CONFIG_DIR} "")
# set(ENV{PKG_CONFIG_LIBDIR} ${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig)
# set(ENV{PKG_CONFIG_SYSROOT_DIR} ${CMAKE_SYSROOT})