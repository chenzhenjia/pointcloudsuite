add_library(pcv_m70_roi_template STATIC
    src/temp_workpiece_interface.cpp
    include/pcv/interface/temp_workpiece_interface.h
)
target_include_directories(pcv_m70_roi_template PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m70_roi_template PUBLIC
    pcv_m10_pointcloud_read
    pcv_m40_pointcloud_matching
    pcv_m50_coordinate_conversion
    pcv_m80_plane_output
    pcv_infrastructure
    pcv_core Qt::Core Qt::Gui)
