# ---------EdgeVision_GUI---------
add_executable(EdgeVision app.cpp EGuiNoRGA.cpp)
target_link_libraries(EdgeVision utils UI model)
target_include_directories(EdgeVision PRIVATE)
install(TARGETS EdgeVision RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/examples)

# ---------PipelineTest---------
add_executable(PipelineTest pipelineTest.cpp)
target_link_libraries(PipelineTest pipeline utils)
target_include_directories(PipelineTest PRIVATE)
install(TARGETS PipelineTest RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/examples)

