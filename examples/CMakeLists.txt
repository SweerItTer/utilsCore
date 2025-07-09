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
target_link_libraries(utils_test utils)
target_include_directories(utils_test PRIVATE ${RKMEDIA_INC})
install(TARGETS utils_test RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/examples)
# ---------model_test---------
