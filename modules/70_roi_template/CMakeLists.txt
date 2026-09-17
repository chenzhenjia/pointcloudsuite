add_library(pcv_m70_roi_template INTERFACE)
target_include_directories(pcv_m70_roi_template INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m70_roi_template INTERFACE pcv_m60_geometry_workpiece)
