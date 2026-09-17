# Compatibility target. The implementation now lives in module 10.
add_library(pcv_io INTERFACE)
target_link_libraries(pcv_io INTERFACE pcv_m10_pointcloud_io)
