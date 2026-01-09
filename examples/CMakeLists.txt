# ---------EdgeVision_GUI---------
# 完整工程
add_executable(EdgeVision app.cpp)
target_link_libraries(EdgeVision pipeline)
target_include_directories(EdgeVision PRIVATE)
install(TARGETS EdgeVision RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/examples)

# ---------CaptureTest---------
# 录像/拍照/切换分辨率等功能测试
add_executable(CaptureTest captureTest.cpp)
target_link_libraries(CaptureTest pipeline utils)
target_include_directories(CaptureTest PRIVATE)
install(TARGETS CaptureTest RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/examples)

# ---------SnowflakeTest---------
# DRM 雪花屏测试
add_executable(SnowflakeTest snowflakeTest.cpp)
target_link_libraries(SnowflakeTest pipeline utils)
target_include_directories(SnowflakeTest PRIVATE)
install(TARGETS SnowflakeTest RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/examples)

# ---------UITest---------
# 独立UI测试
add_executable(UITest uiTest.cpp)
target_link_libraries(UITest pipeline utils)
target_include_directories(UITest PRIVATE)
install(TARGETS UITest RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/examples)

# ---------visionTest---------
# 独立视频流显示测试
add_executable(visionTest visionTest.cpp)
target_link_libraries(visionTest pipeline utils)
target_include_directories(visionTest PRIVATE)
install(TARGETS visionTest RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/examples)