cmake_minimum_required(VERSION 3.5)

project(EdgeVision VERSION 0.1 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG "-g")

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

find_package(Qt5 COMPONENTS Core Quick Widgets Gui OpenGL REQUIRED)

add_subdirectory(src/UI)            # GUI模块
add_subdirectory(src/model)         # 视觉模型模块
add_subdirectory(src/utils)         # 工具模块

add_subdirectory(examples)          # 例程
