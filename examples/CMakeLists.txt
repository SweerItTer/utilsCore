# ---------GPU/CPU DrawTest--------
add_executable(DrawTest GL_CV_DRAW_test.cpp)
target_include_directories(DrawTest
    PUBLIC
        ${CMAKE_SYSROOT}/usr/include/opencv4
        ${CMAKE_SOURCE_DIR}/include/model
    PRIVATE
        ${RKMEDIA_INC}
)

# 连接 OpenCV 库
target_link_libraries(DrawTest 
    PUBLIC
        utils UI
        ${CMAKE_SYSROOT}/usr/lib/libopencv_core.so
        ${CMAKE_SYSROOT}/usr/lib/libopencv_imgproc.so
        ${CMAKE_SYSROOT}/usr/lib/libopencv_imgcodecs.so
        ${CMAKE_SYSROOT}/usr/lib/libopencv_videoio.so
)