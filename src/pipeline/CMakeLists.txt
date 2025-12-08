set(pipelineIncludeDir "${CMAKE_SOURCE_DIR}/include/pipeline/")

file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE HEADERS "${pipelineIncludeDir}/*.h")

add_library(pipeline STATIC
    ${SOURCES}
    ${HEADERS}  # 会在IDE里显示头文件
)

target_include_directories(pipeline 
    PUBLIC
        ${pipelineIncludeDir}
)

target_link_libraries(pipeline 
    PUBLIC utils
)