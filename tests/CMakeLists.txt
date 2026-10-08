set(PCV_APPLICATION_TEST_TARGETS)

if(PCV_BUILD_EXPERIMENTAL_CAMERA)
    add_executable(frame_converter_tests unit/camera/frame_converter_tests.cpp)
    target_link_libraries(frame_converter_tests PRIVATE Qt::Core pcv_m10_pointcloud_io)
    add_test(NAME frame_converter_tests COMMAND frame_converter_tests)

    add_executable(frame_source_tests unit/camera/frame_source_tests.cpp)
    target_include_directories(frame_source_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/modules/10_pointcloud_io/src)
    target_link_libraries(frame_source_tests PRIVATE Qt::Core pcv_m10_pointcloud_io)
    add_test(NAME frame_source_tests COMMAND frame_source_tests)

    add_executable(intensity_image_tests unit/camera/intensity_image_tests.cpp)
    target_link_libraries(intensity_image_tests PRIVATE Qt::Core Qt::Gui pcv_m10_pointcloud_io)
    add_test(NAME intensity_image_tests COMMAND intensity_image_tests)

    list(APPEND PCV_APPLICATION_TEST_TARGETS frame_converter_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS frame_source_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS intensity_image_tests)
endif()

add_executable(application_config_tests unit/infrastructure/application_config_tests.cpp)
target_link_libraries(application_config_tests PRIVATE Qt::Core pcv_infrastructure)
add_test(NAME application_config_tests COMMAND application_config_tests)

add_executable(ply_reader_tests unit/io/ply_reader_tests.cpp)
target_link_libraries(ply_reader_tests PRIVATE Qt::Core pcv_m10_pointcloud_io)
add_test(NAME ply_reader_tests COMMAND ply_reader_tests)

add_executable(cloud_cache_tests unit/io/cloud_cache_tests.cpp)
target_link_libraries(cloud_cache_tests PRIVATE Qt::Core pcv_m10_pointcloud_io)
add_test(NAME cloud_cache_tests COMMAND cloud_cache_tests)

add_executable(artifact_transaction_tests unit/io/artifact_transaction_tests.cpp)
target_link_libraries(artifact_transaction_tests PRIVATE Qt::Core Qt::Gui pcv_m10_pointcloud_io)
add_test(NAME artifact_transaction_tests COMMAND artifact_transaction_tests)

add_executable(downsample_tests unit/filtering/downsample_tests.cpp)
target_link_libraries(downsample_tests PRIVATE Qt::Core pcv_m10_pointcloud_io)
add_test(NAME downsample_tests COMMAND downsample_tests)

add_executable(statistical_filter_tests unit/filtering/statistical_filter_tests.cpp)
target_link_libraries(statistical_filter_tests PRIVATE Qt::Core pcv_m10_pointcloud_io)
add_test(NAME statistical_filter_tests COMMAND statistical_filter_tests)

add_executable(plane_output_tests unit/output/plane_output_tests.cpp)
target_link_libraries(plane_output_tests PRIVATE Qt::Core Qt::Gui pcv_m10_pointcloud_io)
add_test(NAME plane_output_tests COMMAND plane_output_tests)

add_executable(temp_workpiece_interface_tests unit/interface/temp_workpiece_interface_tests.cpp)
target_link_libraries(temp_workpiece_interface_tests PRIVATE
    Qt::Core Qt::Gui pcv_m60_geometry_workpiece pcv_m50_coordinate_conversion)
if(TARGET pcv_m30_pointcloud_stitch)
    # Cross-path check: the scanning JSON entry and the multi-frame entry must
    # return identical robot_base coordinates for the same PLY/XML/poses.
    target_link_libraries(temp_workpiece_interface_tests PRIVATE pcv_m30_pointcloud_stitch)
    target_compile_definitions(temp_workpiece_interface_tests PRIVATE PCV_TEST_HAS_M30_STITCHING=1)
endif()
add_test(NAME temp_workpiece_interface_tests COMMAND temp_workpiece_interface_tests)

if(TARGET pcv_m30_pointcloud_stitch)
    add_executable(stitching_interface_tests unit/interface/stitching_interface_tests.cpp)
    target_link_libraries(stitching_interface_tests PRIVATE Qt::Core Qt::Gui pcv_m30_pointcloud_stitch pcv_m50_coordinate_conversion)
    add_test(NAME stitching_interface_tests COMMAND stitching_interface_tests)
endif()

list(APPEND PCV_APPLICATION_TEST_TARGETS
    application_config_tests
    ply_reader_tests
    cloud_cache_tests
    artifact_transaction_tests
    downsample_tests
    statistical_filter_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS plane_output_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS temp_workpiece_interface_tests)
if(TARGET stitching_interface_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS stitching_interface_tests)
endif()

if(PCV_BUILD_POINTCLOUDVIEW AND PCV_BUILD_EXPERIMENTAL_CAMERA)
    add_executable(live_pipeline_tests
        unit/pointcloudview/live_pipeline_tests.cpp
        ../apps/pointcloudview/live_pipeline.cpp
        ../apps/pointcloudview/live_pipeline.h
    )
    target_include_directories(live_pipeline_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../apps/pointcloudview
    )
    target_link_libraries(live_pipeline_tests PRIVATE
        Qt::Core pcv_core pcv_m10_pointcloud_io)
    add_test(NAME live_pipeline_tests COMMAND live_pipeline_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS live_pipeline_tests)

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
        Qt::Core Qt::Gui pcv_core pcv_infrastructure pcv_m10_pointcloud_io pcv_m60_geometry_workpiece)
    add_test(NAME pointcloudprocessor_obstacle_tests COMMAND pointcloudprocessor_obstacle_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS pointcloudprocessor_obstacle_tests)
endif()

add_executable(handeye_transform_tests unit/registration/handeye_transform_tests.cpp)
target_link_libraries(handeye_transform_tests PRIVATE Qt::Core Qt::Gui pcv_m50_coordinate_conversion)
add_test(NAME handeye_transform_tests COMMAND handeye_transform_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS handeye_transform_tests)

add_executable(roi_matching_tests unit/registration/roi_matching_tests.cpp)
target_link_libraries(roi_matching_tests PRIVATE
    Qt::Core Qt::Gui pcv_m60_geometry_workpiece pcv_m50_coordinate_conversion)
add_test(NAME roi_matching_tests COMMAND roi_matching_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS roi_matching_tests)

if(TARGET pcv_m30_pointcloud_stitch)
    add_executable(seam_fusion_tests unit/registration/seam_fusion_tests.cpp)
    target_link_libraries(seam_fusion_tests PRIVATE Qt::Core Qt::Gui pcv_m30_pointcloud_stitch)
    add_test(NAME seam_fusion_tests COMMAND seam_fusion_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS seam_fusion_tests)
endif()

add_executable(multiframe_registration_tests unit/registration/multiframe_registration_tests.cpp)
target_link_libraries(multiframe_registration_tests PRIVATE Qt::Core Qt::Gui pcv_m30_pointcloud_stitch)
add_test(NAME multiframe_registration_tests COMMAND multiframe_registration_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS multiframe_registration_tests)

add_executable(plane_fitting_tests unit/planefitting/plane_fitting_tests.cpp)
target_link_libraries(plane_fitting_tests PRIVATE Qt::Core Qt::Gui pcv_m60_geometry_workpiece)
add_test(NAME plane_fitting_tests COMMAND plane_fitting_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS plane_fitting_tests)

add_executable(plane_fitting_equivalence_tests_v2 unit/planefitting/plane_fitting_equivalence_tests_v2.cpp)
target_link_libraries(plane_fitting_equivalence_tests_v2 PRIVATE Qt::Core Qt::Gui pcv_m60_geometry_workpiece)
add_test(NAME plane_fitting_equivalence_tests_v2 COMMAND plane_fitting_equivalence_tests_v2)
list(APPEND PCV_APPLICATION_TEST_TARGETS plane_fitting_equivalence_tests_v2)

add_executable(scan_planner_tests unit/scan/scan_planner_tests.cpp)
target_link_libraries(scan_planner_tests PRIVATE Qt::Core Qt::Gui pcv_m60_geometry_workpiece)
add_test(NAME scan_planner_tests COMMAND scan_planner_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS scan_planner_tests)

add_executable(texture_mapping_tests unit/texture/texture_mapping_tests.cpp)
target_link_libraries(texture_mapping_tests PRIVATE Qt::Core Qt::Gui pcv_m60_geometry_workpiece)
add_test(NAME texture_mapping_tests COMMAND texture_mapping_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS texture_mapping_tests)

if(PCV_BUILD_POINTCLOUDVIEW)
    add_executable(texture_experiment_tests
        unit/pointcloudview/texture_experiment_tests_v1.cpp
    )
    target_link_libraries(texture_experiment_tests PRIVATE
        Qt::Core Qt::Gui pcv_m10_pointcloud_io pcv_m60_geometry_workpiece)
    add_test(NAME texture_experiment_tests COMMAND texture_experiment_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS texture_experiment_tests)
endif()

if(TARGET pcv_m20_pointcloud_render)
    add_executable(pointcloud_canvas_contract_tests unit/render/pointcloud_canvas_contract_tests.cpp)
    target_link_libraries(pointcloud_canvas_contract_tests PRIVATE Qt::Core Qt::Gui pcv_m20_pointcloud_render)
    target_include_directories(pointcloud_canvas_contract_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/modules/20_pointcloud_render/src)
    add_test(NAME pointcloud_canvas_contract_tests COMMAND pointcloud_canvas_contract_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS pointcloud_canvas_contract_tests)
endif()

if(WIN32 AND TARGET Qt6::Core AND PCV_APPLICATION_TEST_TARGETS)
    set(PCV_TEST_PATH "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
    set_tests_properties(${PCV_APPLICATION_TEST_TARGETS}
        PROPERTIES ENVIRONMENT "${PCV_TEST_PATH}")
endif()
if(TARGET texture_experiment_tests)
    set_property(TEST texture_experiment_tests APPEND PROPERTY ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(texture_experiment_tests PROPERTIES TIMEOUT 30)
endif()
