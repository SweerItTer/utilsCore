file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE HEADERS "${CMAKE_SOURCE_DIR}/include/v4l2_capture/*.h")

add_library(v4l2_capture STATIC
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(v4l2_capture PUBLIC
    ${CMAKE_SOURCE_DIR}/include/v4l2_capture
)

target_link_libraries(v4l2_capture PRIVATE
    Qt5::Core
    Qt5::Gui
    utils
    pthread
)
