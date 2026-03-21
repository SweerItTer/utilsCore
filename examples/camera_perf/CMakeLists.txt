add_executable(Camera_Capture_Perf camera_capture_perf.cpp)
target_link_libraries(Camera_Capture_Perf PRIVATE utils)
target_compile_features(Camera_Capture_Perf PRIVATE cxx_std_14)

add_custom_command(TARGET Camera_Capture_Perf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/plot_camera_perf.py
            $<TARGET_FILE_DIR:Camera_Capture_Perf>/plot_camera_perf.py
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/run_camera_perf_matrix.py
            $<TARGET_FILE_DIR:Camera_Capture_Perf>/run_camera_perf_matrix.py
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/README.md
            $<TARGET_FILE_DIR:Camera_Capture_Perf>/README.camera_perf.md
    COMMENT "Copy camera perf helpers"
)
