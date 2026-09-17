add_library(pcv_m40_pointcloud_matching INTERFACE)
target_include_directories(pcv_m40_pointcloud_matching INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m40_pointcloud_matching INTERFACE
    pcv_m30_pointcloud_stitch pcv_m60_geometry_workpiece)

add_library(pcv_m40_pointcloudregistration INTERFACE)
target_link_libraries(pcv_m40_pointcloudregistration INTERFACE pcv_m40_pointcloud_matching)
