# ---------EdgeVision_GUI---------
# add_executable(EdgeVision_GUI_SOURCES EGui.cpp)
# target_link_libraries(EdgeVision_GUI_SOURCES UI utils pthread)
# target_include_directories(EdgeVision_GUI_SOURCES PRIVATE ${RKMEIDA_INC})
# install(TARGETS EdgeVision_GUI_SOURCES 
#     RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/examples)
# ---------OpenGL_test---------
# add_executable(OpenGL_test OpenGL_test.cpp)
# target_link_libraries(OpenGL_test UI pthread)
# target_include_directories(OpenGL_test PRIVATE ${RKMEDIA_INC})
# install(TARGETS OpenGL_test RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/examples)
# ---------utils_test---------
add_executable(utils_test utils_test.cpp)
target_link_libraries(utils_test utils model)
target_include_directories(utils_test PRIVATE ${RKMEDIA_INC})
install(TARGETS utils_test RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/examples)
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