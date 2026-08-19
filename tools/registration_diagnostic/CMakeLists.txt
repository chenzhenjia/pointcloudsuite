add_executable(registration_diagnostic
    main.cpp
    ../../apps/pointcloudview/pointcloudprocessor.cpp
    ../../apps/pointcloudview/pointcloudprocessor.h
)
target_include_directories(registration_diagnostic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/pointcloudview)
target_link_libraries(registration_diagnostic PRIVATE
    Qt::Core Qt::Gui pcv_core pcv_infrastructure pcv_io pcv_filtering)
