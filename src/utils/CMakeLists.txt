# 源码收集
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 静态库构建
add_library(utils STATIC ${SOURCES})

# include 路径
target_include_directories(utils
    PUBLIC ${CMAKE_SOURCE_DIR}/include/utils
    PRIVATE
        ${CMAKE_SYSROOT}/usr/include/drm
        ${CMAKE_SYSROOT}/usr/include/rga
)

# 链接系统依赖
target_link_libraries(utils PUBLIC drm rga)

# 确保 PIC 可用于后续合并
set_target_properties(utils PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 编译输出位置：顶层 lib 目录（自动创建）
set_target_properties(utils PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib
)

# 可选安装
install(TARGETS utils ARCHIVE DESTINATION lib)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/utils DESTINATION include/utils)
