add_library(pcv_stitching_ui STATIC
    stitchingwindow.cpp
    stitchingwindow.h
    ui/stitchingwindow.ui
)

set_property(TARGET pcv_stitching_ui PROPERTY AUTOUIC_SEARCH_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui")

target_include_directories(pcv_stitching_ui
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(pcv_stitching_ui
    PUBLIC
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Concurrent
        pcv_core
        pcv_infrastructure
        pcv_m10_pointcloud_io
        pcv_m30_pointcloud_stitch
        pcv_m50_coordinate_conversion
)
