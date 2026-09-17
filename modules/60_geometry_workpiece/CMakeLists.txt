add_library(pcv_m60_geometry_workpiece STATIC
    src/plane_fitting.cpp
    src/roi_matching.cpp
    src/temp_workpiece_geometry.cpp
    src/temp_workpiece_interface.cpp
    include/pcv/planefitting/plane_fitting.h
    include/pcv/workpiece/roi_matching.h
    include/pcv/workpiece/temp_workpiece_geometry.h
    include/pcv/workpiece/temp_workpiece_interface.h
)
target_include_directories(pcv_m60_geometry_workpiece PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m60_geometry_workpiece PUBLIC pcv_core pcv_infrastructure pcv_m10_pointcloud_io pcv_m50_coordinate_conversion Qt::Core Qt::Gui)

add_library(pcv_m60_plane_fitting INTERFACE)
target_link_libraries(pcv_m60_plane_fitting INTERFACE pcv_m60_geometry_workpiece)

add_library(pcv_m60_planefitting INTERFACE)
target_link_libraries(pcv_m60_planefitting INTERFACE pcv_m60_geometry_workpiece)
