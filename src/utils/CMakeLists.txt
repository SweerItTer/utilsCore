file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE HEADERS "${CMAKE_SOURCE_DIR}/include/utils/*.h")

find_package(OpenCV REQUIRED)

add_library(utils STATIC
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(utils PUBLIC
    # ${OpenCV_INCLUDE_DIRS}
    # ${RKMEDIA_INC}
    ${CMAKE_SYSROOT}/usr/include/drm
    ${CMAKE_SOURCE_DIR}/include/utils
)

target_link_libraries(utils PUBLIC
    # ${OpenCV_LIBS}
    drm
)
