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


if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # NOTE: Don't call find_program() with CMAKE_AR/CMAKE_RANLIB directly.
    # Those variables are typically pre-populated (and cached) by CMake to the plain
    # cross 'ar/ranlib', which can't handle LTO objects without the GCC plugin.
    find_program(GCC_AR
        NAMES aarch64-buildroot-linux-gnu-gcc-ar aarch64-linux-gnu-gcc-ar gcc-ar
        PATHS "${TOOLCHAIN_PATH}/bin"
        NO_DEFAULT_PATH
    )
    find_program(GCC_RANLIB
        NAMES aarch64-buildroot-linux-gnu-gcc-ranlib aarch64-linux-gnu-gcc-ranlib gcc-ranlib
        PATHS "${TOOLCHAIN_PATH}/bin"
        NO_DEFAULT_PATH
    )

    if(NOT GCC_AR)
        find_program(GCC_AR NAMES aarch64-buildroot-linux-gnu-gcc-ar aarch64-linux-gnu-gcc-ar gcc-ar)
    endif()
    if(NOT GCC_RANLIB)
        find_program(GCC_RANLIB NAMES aarch64-buildroot-linux-gnu-gcc-ranlib aarch64-linux-gnu-gcc-ranlib gcc-ranlib)
    endif()

    if(GCC_AR AND GCC_RANLIB)
        set(CMAKE_AR "${GCC_AR}" CACHE FILEPATH "Archiver" FORCE)
        set(CMAKE_RANLIB "${GCC_RANLIB}" CACHE FILEPATH "Ranlib" FORCE)
        message(STATUS "Using LTO-capable archiver: ${CMAKE_AR}")
    else()
        message(WARNING "gcc-ar/gcc-ranlib not found; static libraries may be incomplete.")
    endif()
endif()