cmake_minimum_required(VERSION 3.14)
project(EdgeVision VERSION 1.0)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG  "${CMAKE_CXX_FLAGS_DEBUG} -g")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -ffunction-sections -fdata-sections")

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

# 启用 ccache
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Release 下额外优化
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # 仅 Release 构建使用 -march=armv8.2-a（交叉编译或者 native）
    if(USE_CROSS_COMPILE)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=armv8.2-a")
    else()
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")
    endif()

    # 链接时移除未使用段
    add_link_options(-Wl,--gc-sections)
endif()

find_package(Qt5 COMPONENTS Core Quick Widgets Gui OpenGL REQUIRED)

add_subdirectory(src/UI)            # GUI模块
add_subdirectory(src/model)         # 视觉模型模块
add_subdirectory(src/utils)         # 工具模块

add_subdirectory(examples)          # 例程