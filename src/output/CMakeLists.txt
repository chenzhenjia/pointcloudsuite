# Compatibility target. The implementation now lives in module 10.
add_library(pcv_output INTERFACE)
target_link_libraries(pcv_output INTERFACE pcv_m10_pointcloud_io)
