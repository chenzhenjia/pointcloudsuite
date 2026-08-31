# Compatibility target. The implementation now lives in modules/10_pointcloudread.
add_library(pcv_io INTERFACE)
target_link_libraries(pcv_io INTERFACE pcv_m10_pointcloudread)
