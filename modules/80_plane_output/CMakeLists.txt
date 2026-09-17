add_library(pcv_m80_plane_output INTERFACE)
target_include_directories(pcv_m80_plane_output INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m80_plane_output INTERFACE pcv_m10_pointcloud_io)

add_library(pcv_m80_planeoutput INTERFACE)
target_link_libraries(pcv_m80_planeoutput INTERFACE pcv_m80_plane_output)
