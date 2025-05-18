cmake_minimum_required(VERSION 3.5)

project(EdgeVision VERSION 0.1 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 是否使用交叉编译工具链的开关
option(USE_CROSS_COMPILE "Enable cross-compilation" OFF)
if (USE_CROSS_COMPILE)
    add_definitions(-DRV1126)

    # 使用工具链文件
    include(CMakeForceCompiler) # CMake 3.6 版本之前
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/toolchain.cmake)

    # 设置库路径
    link_directories(${CMAKE_SYSROOT}/usr/lib)

endif()

find_package(Qt5 COMPONENTS Core Quick Widgets Gui REQUIRED)

add_subdirectory(src/UI)            # GUI模块
add_subdirectory(src/model)         # 视觉模型模块
add_subdirectory(src/utils)         # 工具模块
add_subdirectory(src/v4l2_capture)  # 图像采集模块

# ---------EdgeVision_GUI---------
set(EdgeVision_GUI_SOURCES examples/EGui.cpp)

add_executable(EdgeVision_GUI ${EdgeVision_GUI_SOURCES})

target_link_libraries(EdgeVision_GUI
    PRIVATE model UI v4l2_capture utils
    pthread
)

# ---------OpenGL_test---------
# ---------v4l2_test---------
# ---------model_test---------
