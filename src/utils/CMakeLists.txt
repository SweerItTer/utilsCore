file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE HEADERS "${CMAKE_SOURCE_DIR}/include/utils/*.h")

add_library(utils STATIC
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(utils PUBLIC
    ${CMAKE_SOURCE_DIR}/include/utils
)

target_link_libraries(utils PRIVATE
    Qt5::Core
)
