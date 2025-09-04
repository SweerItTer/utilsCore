set(utilsIncludeDir "${CMAKE_SOURCE_DIR}/include/utils/")

file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE HEADERS "${utilsIncludeDir}/*.h")

add_library(utils STATIC
    ${SOURCES}
    ${HEADERS}  # 会在IDE里显示头文件
)

target_include_directories(utils 
    PUBLIC
        ${utilsIncludeDir}
        ${CMAKE_SYSROOT}/usr/include/rga
        ${CMAKE_SYSROOT}/usr/include/drm
)

target_link_libraries(utils 
    PUBLIC drm rga udev
)
