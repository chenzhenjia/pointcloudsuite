add_executable(registration_diagnostic
    main.cpp
    ../../apps/pointcloudstitch/pointcloudprocessor.cpp
    ../../apps/pointcloudstitch/pointcloudprocessor.h
)
target_include_directories(registration_diagnostic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/pointcloudstitch)
target_link_libraries(registration_diagnostic PRIVATE
    Qt::Core Qt::Gui pcv_core pcv_infrastructure pcv_m10_pointcloudread
    pcv_m30_pointcloudstitch pcv_m40_pointcloudregistration
    pcv_m50_coordinateconversion)
