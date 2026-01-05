# ---------GPU/CPU DrawTest--------
add_executable(RGACVDrawTest RGA_CV_DRAW_test.cpp)
target_include_directories(RGACVDrawTest
    PUBLIC
        ${CMAKE_SYSROOT}/usr/include/opencv4
        ${CMAKE_SOURCE_DIR}/include/model
    PRIVATE
        ${CMAKE_SOURCE_DIR}/examples
)

# 连接 OpenCV 库
target_link_libraries(RGACVDrawTest 
    PUBLIC
        utils
        ${CMAKE_SYSROOT}/usr/lib/libopencv_core.so
        ${CMAKE_SYSROOT}/usr/lib/libopencv_imgproc.so
        ${CMAKE_SYSROOT}/usr/lib/libopencv_imgcodecs.so
        ${CMAKE_SYSROOT}/usr/lib/libopencv_videoio.so
)