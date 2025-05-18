# 设置自动处理 .ui 路径
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/uis")

# 查找源码、头文件、UI文件
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE HEADERS "${CMAKE_SOURCE_DIR}/include/UI/*.h")
file(GLOB_RECURSE UI_FILES "${CMAKE_CURRENT_SOURCE_DIR}/uis/*.ui")

# 查找 Qt 模块
find_package(Qt5 COMPONENTS Core Quick Widgets Gui REQUIRED)

# 添加 UI 模块库
add_library(UI STATIC
    ${SOURCES}
    ${HEADERS}
    ${UI_FILES}
    ${RESOURCE_FILES}
)

target_sources(UI PRIVATE ${UI_RESOURCES})

# 链接 Qt 库
target_link_libraries(UI PRIVATE
    Qt5::Core
    Qt5::Quick
    Qt5::Widgets
    Qt5::Gui
)
