add_library(pcv_m30_pointcloudstitch STATIC
    src/stitching_interface.cpp
    include/pcv/interface/stitching_interface.h
)
target_include_directories(pcv_m30_pointcloudstitch PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m30_pointcloudstitch PUBLIC
    pcv_m40_pointcloudregistration
    pcv_m50_coordinateconversion
    pcv_m10_pointcloudread
    pcv_output
    Qt::Core Qt::Gui)
