set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/uis")

file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE HEADERS "${CMAKE_SOURCE_DIR}/include/UI/*.h")
file(GLOB_RECURSE UI_FILES "${CMAKE_CURRENT_SOURCE_DIR}/uis/*.ui")
file(GLOB_RECURSE QRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/icon/*.qrc")

add_executable(OpenGLCamera-rk3566
    ${SOURCES}
    ${HEADERS}
    ${UI_FILES}
    ${QRC_FILES}
)

target_include_directories(OpenGLCamera-rk3566
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include/UI
        ${CMAKE_SOURCE_DIR}/include/utils
        ${CMAKE_SYSROOT}/usr/include/EGL
        ${CMAKE_SYSROOT}/usr/include/drm
        ${CMAKE_SYSROOT}/usr/include/rga
)

target_link_libraries(OpenGLCamera-rk3566
    PRIVATE
        Qt5::Widgets
        Qt5::OpenGL
        Qt5::Gui
        Qt5::Core
        Qt5::Quick
        ${CMAKE_SOURCE_DIR}/lib/libutils.a
        drm rga mali EGL pthread
)

install(TARGETS OpenGLCamera-rk3566 RUNTIME DESTINATION bin)
