# 设置自动处理 .ui 路径
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/uis")

# 查找源文件
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# 头文件在 include/UI 目录
file(GLOB_RECURSE HEADERS "${PROJECT_SOURCE_DIR}/include/UI/*.h")

# 查找 .ui 文件
file(GLOB_RECURSE UI_FILES "${CMAKE_CURRENT_SOURCE_DIR}/uis/*.ui")
# .qrc 文件
file(GLOB_RECURSE QRC_FILES "${PROJECT_SOURCE_DIR}/src/UI/res/*.qrc")

# 添加 UI 模块库
add_library(UI STATIC
    ${SOURCES}
    ${HEADERS}
    ${UI_FILES}
    ${QRC_FILES}
)

# 设置头文件查找路径
target_include_directories(UI PUBLIC
    ${PROJECT_SOURCE_DIR}/include/UI
)


# 链接 Qt 库和 utils
target_link_libraries(UI PUBLIC
    utils
    Qt5::Widgets
    Qt5::OpenGL
    Qt5::Core
    Qt5::Quick
    Qt5::Gui
)
