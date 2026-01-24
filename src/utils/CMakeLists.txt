set(UTILS_INCLUDE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../include/utils/")

# 收集所有源文件
file(GLOB_RECURSE UTILS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

file(GLOB_RECURSE UTILS_HEADERS
    "${UTILS_INCLUDE_ROOT}/*.h"
)

# 构建静态库
add_library(utils OBJECT
    ${UTILS_SOURCES}
    ${UTILS_HEADERS}
)

# 对外只暴露 include 根目录
target_include_directories(utils
    PUBLIC
        ${UTILS_INCLUDE_ROOT}
        $<$<BOOL:${CMAKE_SYSROOT}>:${CMAKE_SYSROOT}/usr/include/drm>
        $<$<BOOL:${CMAKE_SYSROOT}>:${CMAKE_SYSROOT}/usr/include/rga>
)

# 链接系统库
target_link_libraries(utils
    PRIVATE
        drm
        rga
        udev
        rockchip_mpp
        pthread
)

# 提供别名
add_library(utilsCore::utils ALIAS utils)

# 可选生成静态库
option(BUILD_STATIC_UTILS "Build utilsCore as static library for testing" OFF)

if(BUILD_STATIC_UTILS)
    add_library(utils_static STATIC $<TARGET_OBJECTS:utils>)
    target_include_directories(utils_static PUBLIC ${UTILS_INCLUDE_ROOT})
    target_link_libraries(utils_static PRIVATE drm rga udev rockchip_mpp pthread)

    # 给静态库起别名
    add_library(utilsCore::utils ALIAS utils_static)
endif()