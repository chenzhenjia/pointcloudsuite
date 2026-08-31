# Compatibility target. Implementations now live in modules 40 and 50.
add_library(pcv_registration INTERFACE)
target_link_libraries(pcv_registration INTERFACE
    pcv_m40_pointcloudregistration
    pcv_m50_coordinateconversion)
