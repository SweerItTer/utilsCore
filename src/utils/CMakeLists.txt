set(UTILS_INCLUDE_ROOT "${CMAKE_SOURCE_DIR}/include/utils/")


file(GLOB_RECURSE UTILS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

file(GLOB_RECURSE UTILS_HEADERS
    "${UTILS_INCLUDE_ROOT}/utils/*.h"
)

add_library(utils STATIC
    ${UTILS_SOURCES}
    ${UTILS_HEADERS}
)

# 对外只暴露 include 根目录
target_include_directories(utils
    PUBLIC
        ${UTILS_INCLUDE_ROOT}
        ${CMAKE_SYSROOT}/usr/include/drm
        ${CMAKE_SYSROOT}/usr/include/rga
)

# 底层库是 utils 的实现细节
target_link_libraries(utils
    PRIVATE
        drm
        rga
        udev
        rockchip_mpp
)