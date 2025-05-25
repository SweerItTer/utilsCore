set(RKMEIDA_INC ${RKMEDIA_DIR}/include)

# ---------EdgeVision_GUI---------
# set(EdgeVision_GUI_SOURCES examples/EGui.cpp)

# add_executable(EdgeVision_GUI ${EdgeVision_GUI_SOURCES})

# target_link_libraries(EdgeVision_GUI
#     PRIVATE 
#     model UI utils
#     pthread
# )

# ---------OpenGL_test---------
# set(OpenGL_TEST_SOURCES examples/OpenGL_test.cpp)

# add_executable(OpenGL_test ${OpenGL_TEST_SOURCES})

# target_link_libraries(OpenGL_test
#     PRIVATE 
#     UI
#     pthread
# )
# ---------utils_test---------
add_executable(utils_test utils_test.cpp)
target_link_libraries(utils_test utils)
target_include_directories(utils_test PRIVATE ${RKMEIDA_INC})
install(TARGETS utils_test RUNTIME DESTINATION "bin")
# ---------model_test---------