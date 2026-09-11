add_library(pcv_m40_pointcloud_matching STATIC
    src/multiframe_registration.cpp
    src/roi_matching.cpp
    include/pcv/registration/multiframe_registration.h
    include/pcv/matching/roi_matching.h
    include/pcv/registration/seam_fusion.h
    include/pcv/registration/seam_fusion_kernel.h
    include/pcv/registration/seam_fusion_types.h
)
target_include_directories(pcv_m40_pointcloud_matching PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m40_pointcloud_matching PUBLIC
    pcv_core pcv_filtering pcv_m50_coordinate_conversion Qt::Core Qt::Gui Qt::Concurrent)

add_library(pcv_m40_pointcloudregistration INTERFACE)
target_link_libraries(pcv_m40_pointcloudregistration INTERFACE pcv_m40_pointcloud_matching)
