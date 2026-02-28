add_executable(LoggerCfg_Demo logger_config_demo.cpp)
target_link_libraries(LoggerCfg_Demo utils)
target_compile_features(LoggerCfg_Demo PRIVATE cxx_std_14)

add_executable(LoggerV2_Demo logger_v2_demo.cpp)
target_link_libraries(LoggerV2_Demo utils)
target_compile_features(LoggerV2_Demo PRIVATE cxx_std_14)

add_executable(TCP_Test tcpServer_example.cpp)
target_link_libraries(TCP_Test utils)
target_compile_features(TCP_Test PRIVATE cxx_std_14)

add_custom_command(TARGET TCP_Test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/www
            $<TARGET_FILE_DIR:TCP_Test>/www
    COMMENT "Copy example www/ to output directory"
)

add_executable(ThreadPool_Scale_Demo threadpool_scale_demo.cpp)
target_link_libraries(ThreadPool_Scale_Demo utils)
target_compile_features(ThreadPool_Scale_Demo PRIVATE cxx_std_14)

add_executable(RGA_DmaBuf_Demo rga_dmabuf_demo.cpp)
target_link_libraries(RGA_DmaBuf_Demo utils)
target_compile_features(RGA_DmaBuf_Demo PRIVATE cxx_std_14)