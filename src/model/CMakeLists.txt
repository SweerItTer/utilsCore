# 查找源码和头文件
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE HEADERS "${CMAKE_SOURCE_DIR}/include/model/*.h")

add_library(model STATIC
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(model PUBLIC
    ${CMAKE_SOURCE_DIR}/include/model
)

target_link_libraries(model PRIVATE
)
