add_library(pcv_m30_pointcloud_stitch STATIC
    src/stitching_interface.cpp
    src/seam_fusion.cpp
    src/multiframe_registration.cpp
    include/pcv/interface/stitching_interface.h
    include/pcv/stitching/seam_fusion.h
    include/pcv/stitching/seam_fusion_kernel.h
    include/pcv/stitching/seam_fusion_types.h
    include/pcv/stitching/multiframe_registration.h
)
target_include_directories(pcv_m30_pointcloud_stitch PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m30_pointcloud_stitch PUBLIC
    pcv_m50_coordinate_conversion
    pcv_m10_pointcloud_io
    Qt::Core Qt::Gui Qt::Concurrent)

add_library(pcv_m30_pointcloudstitch INTERFACE)
target_link_libraries(pcv_m30_pointcloudstitch INTERFACE pcv_m30_pointcloud_stitch)
