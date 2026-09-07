# Compatibility target. The implementation now lives in modules/10_pointcloud_read.
add_library(pcv_io INTERFACE)
target_link_libraries(pcv_io INTERFACE pcv_m10_pointcloud_read)
