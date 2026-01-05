# ---------Diffrerent re --------
file(GLOB_RECURSE HEADER "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_executable(CaptrueTest DIF_RES_CAP.cpp ${HEADER})
target_include_directories(CaptrueTest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(CaptrueTest PUBLIC utils pipeline)