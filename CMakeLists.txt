cmake_minimum_required(VERSION 3.14)
project(EdgeVision VERSION 1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

# 是否使用交叉编译工具链的开关
option(USE_CROSS_COMPILE "Enable cross-compilation for RK3568" OFF)

if(USE_CROSS_COMPILE)
    # RK3568 宏定义
    add_definitions(-DRK3568)
    
    # 设置工具链文件路径
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/rk3568-toolchain.cmake)
    # 设置库路径
    link_directories(${CMAKE_SYSROOT}/usr/lib)

endif()

# 启用 ccache 加速编译
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()
# 优化编译选项
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        if(USE_CROSS_COMPILE)
            set(march armv8.2-a)
        else()
            set(march native)
        endif()
        add_compile_options(
            -O2                    # 最高优化级别
            -march=${march}        # 本机编译时使用 native
            -ffunction-sections    # 优化未使用的函数
            -fdata-sections        # 优化未使用的数据
        )
        
        add_link_options(
            -Wl,--gc-sections     # 移除未使用的段
        )
    endif()
endif()

find_package(Qt5 COMPONENTS Core Quick Widgets Gui OpenGL REQUIRED)

add_subdirectory(src/UI)            # GUI模块
add_subdirectory(src/model)         # 视觉模型模块
add_subdirectory(src/utils)         # 工具模块

add_subdirectory(examples)          # 例程