# Compatibility target. The implementation now lives in module 80.
add_library(pcv_output INTERFACE)
target_link_libraries(pcv_output INTERFACE pcv_m80_planeoutput)
