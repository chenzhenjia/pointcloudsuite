# Compatibility target. Implementations now live in modules 30 and 70.
add_library(pcv_interface INTERFACE)
target_link_libraries(pcv_interface INTERFACE
    pcv_m30_pointcloudstitch
    pcv_m70_roi_template)
