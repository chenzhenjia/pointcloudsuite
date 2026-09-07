add_executable(registration_diagnostic
    main.cpp
)
target_link_libraries(registration_diagnostic PRIVATE
    pcv_m10_pointcloud_read
    pcv_m30_pointcloud_stitch
    pcv_m40_pointcloud_matching
    pcv_m50_coordinate_conversion
    pcv_core
    pcv_infrastructure
    Qt::Core Qt::Gui)
