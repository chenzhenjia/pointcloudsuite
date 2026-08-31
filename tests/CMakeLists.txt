set(PCV_APPLICATION_TEST_TARGETS)

add_executable(application_config_tests unit/infrastructure/application_config_tests.cpp)
target_link_libraries(application_config_tests PRIVATE Qt::Core pcv_infrastructure)
add_test(NAME application_config_tests COMMAND application_config_tests)

add_executable(ply_reader_tests unit/io/ply_reader_tests.cpp)
target_link_libraries(ply_reader_tests PRIVATE Qt::Core pcv_m10_pointcloudread)
add_test(NAME ply_reader_tests COMMAND ply_reader_tests)

add_executable(cloud_cache_tests unit/io/cloud_cache_tests.cpp)
target_link_libraries(cloud_cache_tests PRIVATE Qt::Core pcv_m10_pointcloudread)
add_test(NAME cloud_cache_tests COMMAND cloud_cache_tests)

add_executable(downsample_tests unit/filtering/downsample_tests.cpp)
target_link_libraries(downsample_tests PRIVATE Qt::Core pcv_filtering)
add_test(NAME downsample_tests COMMAND downsample_tests)

add_executable(statistical_filter_tests unit/filtering/statistical_filter_tests.cpp)
target_link_libraries(statistical_filter_tests PRIVATE Qt::Core pcv_filtering)
add_test(NAME statistical_filter_tests COMMAND statistical_filter_tests)

add_executable(plane_output_tests unit/output/plane_output_tests.cpp)
target_link_libraries(plane_output_tests PRIVATE Qt::Core Qt::Gui pcv_m80_planeoutput pcv_m10_pointcloudread)
add_test(NAME plane_output_tests COMMAND plane_output_tests)

add_executable(temp_workpiece_interface_tests unit/interface/temp_workpiece_interface_tests.cpp)
target_link_libraries(temp_workpiece_interface_tests PRIVATE Qt::Core Qt::Gui pcv_m70_roi_template)
add_test(NAME temp_workpiece_interface_tests COMMAND temp_workpiece_interface_tests)

add_executable(stitching_interface_tests unit/interface/stitching_interface_tests.cpp)
target_link_libraries(stitching_interface_tests PRIVATE Qt::Core Qt::Gui pcv_m30_pointcloudstitch pcv_m50_coordinateconversion)
add_test(NAME stitching_interface_tests COMMAND stitching_interface_tests)

list(APPEND PCV_APPLICATION_TEST_TARGETS
    application_config_tests
    ply_reader_tests
    cloud_cache_tests
    downsample_tests
    statistical_filter_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS plane_output_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS temp_workpiece_interface_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS stitching_interface_tests)

if(PCV_BUILD_POINTCLOUDVIEW)
    add_executable(pointcloudprocessor_obstacle_tests
        unit/pointcloudview/pointcloudprocessor_obstacle_tests.cpp
        ../apps/pointcloudview/pointcloudprocessor.cpp
        ../apps/pointcloudview/pointcloudprocessor.h
    )
    target_include_directories(pointcloudprocessor_obstacle_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../apps/pointcloudview
    )
    target_link_libraries(pointcloudprocessor_obstacle_tests PRIVATE
        Qt::Core Qt::Gui pcv_core pcv_infrastructure pcv_m10_pointcloudread pcv_filtering pcv_m60_planefitting pcv_m70_roi_template)
    add_test(NAME pointcloudprocessor_obstacle_tests COMMAND pointcloudprocessor_obstacle_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS pointcloudprocessor_obstacle_tests)
endif()

add_executable(handeye_transform_tests unit/registration/handeye_transform_tests.cpp)
target_link_libraries(handeye_transform_tests PRIVATE Qt::Core Qt::Gui pcv_m50_coordinateconversion)
add_test(NAME handeye_transform_tests COMMAND handeye_transform_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS handeye_transform_tests)

add_executable(seam_fusion_tests unit/registration/seam_fusion_tests.cpp)
target_link_libraries(seam_fusion_tests PRIVATE Qt::Core Qt::Gui pcv_m40_pointcloudregistration)
add_test(NAME seam_fusion_tests COMMAND seam_fusion_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS seam_fusion_tests)

add_executable(multiframe_registration_tests unit/registration/multiframe_registration_tests.cpp)
target_link_libraries(multiframe_registration_tests PRIVATE Qt::Core Qt::Gui pcv_m40_pointcloudregistration)
add_test(NAME multiframe_registration_tests COMMAND multiframe_registration_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS multiframe_registration_tests)

add_executable(plane_fitting_tests unit/planefitting/plane_fitting_tests.cpp)
target_link_libraries(plane_fitting_tests PRIVATE Qt::Core Qt::Gui pcv_m60_planefitting)
add_test(NAME plane_fitting_tests COMMAND plane_fitting_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS plane_fitting_tests)

add_executable(pointcloud_canvas_contract_tests unit/render/pointcloud_canvas_contract_tests.cpp)
target_link_libraries(pointcloud_canvas_contract_tests PRIVATE Qt::Core Qt::Gui pcv_m20_pointcloudrender)
add_test(NAME pointcloud_canvas_contract_tests COMMAND pointcloud_canvas_contract_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS pointcloud_canvas_contract_tests)

if(WIN32 AND TARGET Qt6::Core AND PCV_APPLICATION_TEST_TARGETS)
    set(PCV_TEST_PATH "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
    set_tests_properties(${PCV_APPLICATION_TEST_TARGETS}
        PROPERTIES ENVIRONMENT "${PCV_TEST_PATH}")
endif()
