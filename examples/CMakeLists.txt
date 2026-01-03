# ---------model_test---------
# add_executable(rknn_test rknn_test.cpp)
# target_link_libraries(rknn_test utils model)
# target_include_directories(rknn_test PRIVATE ${RKMEDIA_INC})

# # 编译后自动复制到构建目录的对应位置
# add_custom_command(TARGET rknn_test POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/examples/model
#     COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:rknn_test> ${CMAKE_BINARY_DIR}/examples/model/
#     COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/include/model/res/bus.jpg ${CMAKE_BINARY_DIR}/examples/model/
#     COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/include/model/res/image.png ${CMAKE_BINARY_DIR}/examples/model/
#     COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/include/model/res/BiliBili.mp4 ${CMAKE_BINARY_DIR}/examples/model/
#     COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/include/model/res/yolov5s_relu.rknn ${CMAKE_BINARY_DIR}/examples/model/
#     COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/include/model/res/coco_80_labels_list.txt ${CMAKE_BINARY_DIR}/examples/model/
# )

# ---------model_test---------
add_executable(ModelPerTest SS_AS_AM1_AM3_test.cpp)
target_link_libraries(ModelPerTest utils model)
target_include_directories(ModelPerTest PRIVATE ${RKMEDIA_INC})

# 编译后自动复制到构建目录的对应位置
add_custom_command(TARGET ModelPerTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/examples/model
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ModelPerTest> ${CMAKE_BINARY_DIR}/examples/model/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/include/model/res/BiliBili.mp4 ${CMAKE_BINARY_DIR}/examples/model/test.mp4
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/include/model/res/yolov5s_relu.rknn ${CMAKE_BINARY_DIR}/examples/model/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/include/model/res/coco_80_labels_list.txt ${CMAKE_BINARY_DIR}/examples/model/
)