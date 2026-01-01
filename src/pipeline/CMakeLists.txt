set(PIPELINE_INCLUDE_ROOT "${CMAKE_SOURCE_DIR}/include/pipeline/")

file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE HEADERS "${PIPELINE_INCLUDE_ROOT}/*.h")

add_library(pipeline STATIC
    ${SOURCES}
    ${HEADERS}  # 会在IDE里显示头文件
)

target_include_directories(pipeline 
    PUBLIC
        ${PIPELINE_INCLUDE_ROOT}
)

target_link_libraries(pipeline 
    PUBLIC utils model UI 
)