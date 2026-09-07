# Compatibility target. Implementations now live in modules 40 and 50.
add_library(pcv_registration INTERFACE)
target_link_libraries(pcv_registration INTERFACE
    pcv_m40_pointcloud_matching
    pcv_m50_coordinate_conversion
    pcv_m30_pointcloud_stitch)
