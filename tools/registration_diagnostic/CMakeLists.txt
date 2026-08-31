add_executable(registration_diagnostic
    main.cpp
)
target_link_libraries(registration_diagnostic PRIVATE
    pcv_m10_pointcloudread
    pcv_m30_pointcloudstitch
    pcv_m40_pointcloudregistration
    pcv_m50_coordinateconversion
    pcv_core
    pcv_infrastructure
    Qt::Core Qt::Gui)
