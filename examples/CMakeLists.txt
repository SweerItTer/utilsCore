# ---------EdgeVision_GUI---------
add_executable(EdgeVision app.cpp EGui.cpp)
target_link_libraries(EdgeVision utils UI model)
target_include_directories(EdgeVision PRIVATE)
install(TARGETS EdgeVision RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/examples)

