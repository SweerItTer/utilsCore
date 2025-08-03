cmake_minimum_required(VERSION 3.5)
project(EdgeVision VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(USE_CROSS_COMPILE "Enable cross-compilation for RK3568" OFF)

if(USE_CROSS_COMPILE)
    add_definitions(-DRK3568)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/rk3568-toolchain.cmake)
    link_directories(${CMAKE_SYSROOT}/usr/lib)
endif()

find_package(Qt5 COMPONENTS Core Quick Widgets Gui OpenGL REQUIRED)

# 链接 utils 编译出来的静态库即可，不加源码

# add_subdirectory(src/utils)
# add_subdirectory(src/UI)
