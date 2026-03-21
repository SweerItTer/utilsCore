add_executable(LoggerCfg_Demo logger_config_demo.cpp)
target_link_libraries(LoggerCfg_Demo utils)
target_compile_features(LoggerCfg_Demo PRIVATE cxx_std_14)

add_executable(LoggerV2_Demo logger_v2_demo.cpp)
target_link_libraries(LoggerV2_Demo utils)
target_compile_features(LoggerV2_Demo PRIVATE cxx_std_14)

add_library(Net_Demo_Plugin MODULE net_demo_plugin.cpp)
target_link_libraries(Net_Demo_Plugin PRIVATE utils_net)
target_compile_features(Net_Demo_Plugin PRIVATE cxx_std_14)
set_target_properties(Net_Demo_Plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "net_demo_plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins"
)

add_executable(Net_Http_Demo tcpServer_example.cpp)
target_link_libraries(Net_Http_Demo utils_net)
target_compile_features(Net_Http_Demo PRIVATE cxx_std_14)

add_custom_command(TARGET Net_Http_Demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/net_demo.json
            $<TARGET_FILE_DIR:Net_Http_Demo>/net_demo.json
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/www
            $<TARGET_FILE_DIR:Net_Http_Demo>/www
    COMMENT "Copy net demo config and www assets"
)

add_executable(ThreadPool_Scale_Demo threadpool_scale_demo.cpp)
target_link_libraries(ThreadPool_Scale_Demo utils)
target_compile_features(ThreadPool_Scale_Demo PRIVATE cxx_std_14)

add_executable(RGA_DmaBuf_Demo rga_dmabuf_demo.cpp)
target_link_libraries(RGA_DmaBuf_Demo utils)
target_compile_features(RGA_DmaBuf_Demo PRIVATE cxx_std_14)

add_executable(StaticCallback_Compile_Demo static_callback_compile_demo.cpp)
target_link_libraries(StaticCallback_Compile_Demo utils)
target_compile_features(StaticCallback_Compile_Demo PRIVATE cxx_std_14)

add_subdirectory(camera_perf utils_camera_perf)
