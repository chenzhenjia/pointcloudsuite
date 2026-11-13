add_library(pcv_m40_pointcloud_matching STATIC src/template_match_v1.cpp include/pcv/matching/template_match_v1.h)
target_include_directories(pcv_m40_pointcloud_matching PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m40_pointcloud_matching PUBLIC
    pcv_m30_pointcloud_stitch pcv_m60_geometry_workpiece pcv_m50_coordinate_conversion)
