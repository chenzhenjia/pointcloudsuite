add_library(pcv_m30_pointcloudstitch INTERFACE)
target_link_libraries(pcv_m30_pointcloudstitch INTERFACE
    pcv_m40_pointcloudregistration
    pcv_output
    pcv_interface)
