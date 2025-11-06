# 查找源码和头文件
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE HEADERS "${CMAKE_SOURCE_DIR}/include/model/*.h")

add_library(model STATIC
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(model PUBLIC
    ${CMAKE_SYSROOT}/usr/include/opencv4
    ${CMAKE_SOURCE_DIR}/include/model
)

# 连接 OpenCV 库
target_link_libraries(model PUBLIC     
    utils
    ${CMAKE_SYSROOT}/usr/lib/librknn_api.so

    ${CMAKE_SYSROOT}/usr/lib/libopencv_core.so
    ${CMAKE_SYSROOT}/usr/lib/libopencv_imgproc.so
    ${CMAKE_SYSROOT}/usr/lib/libopencv_imgcodecs.so
    ${CMAKE_SYSROOT}/usr/lib/libopencv_videoio.so
)
