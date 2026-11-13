include_directories("${CMAKE_CURRENT_SOURCE_DIR}/support/fixtures")
set(PCV_APPLICATION_TEST_TARGETS)

if(PCV_BUILD_EXPERIMENTAL_CAMERA)
    get_target_property(PCV_CAMERA_TEST_DEFINITIONS pcv_m10_pointcloud_io INTERFACE_COMPILE_DEFINITIONS)
    if("PCV_CAMERA_HAVE_HIKSDK=1" IN_LIST PCV_CAMERA_TEST_DEFINITIONS)
        add_executable(depth_raw_export_tests unit/camera/depth_raw_export_tests.cpp)
        target_include_directories(depth_raw_export_tests PRIVATE ../modules/10_pointcloud_io/src)
        target_link_libraries(depth_raw_export_tests PRIVATE pcv_m10_pointcloud_io)
        add_test(NAME depth_raw_export_tests COMMAND depth_raw_export_tests)
        list(APPEND PCV_APPLICATION_TEST_TARGETS depth_raw_export_tests)
    endif()
    add_executable(depth_capture_tests unit/camera/depth_capture_tests.cpp)
    target_link_libraries(depth_capture_tests PRIVATE pcv_m10_pointcloud_io pcv_m30_pointcloud_stitch)
    add_test(NAME depth_capture_tests COMMAND depth_capture_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS depth_capture_tests)
    if(PCV_BUILD_EXPERIMENTAL_TESTS AND "PCV_CAMERA_HAVE_HIKSDK=1" IN_LIST PCV_CAMERA_TEST_DEFINITIONS)
        # Compile the actual driver with SDK declarations but local fake symbols.
        # Its definitions resolve before the static archive's real SDK driver.
        get_target_property(PCV_CAMERA_TEST_INCLUDES pcv_m10_pointcloud_io INCLUDE_DIRECTORIES)
        add_executable(mv3d_startup_tests experimental/camera/mv3d_startup_tests.cpp
            ../modules/10_pointcloud_io/src/mv3d_lp_camera.cpp)
        target_include_directories(mv3d_startup_tests PRIVATE ${PCV_CAMERA_TEST_INCLUDES})
        target_compile_definitions(mv3d_startup_tests PRIVATE MV3D_LP_API=)
        target_link_libraries(mv3d_startup_tests PRIVATE pcv_m10_pointcloud_io)
        add_test(NAME mv3d_startup_tests COMMAND mv3d_startup_tests)
        set_tests_properties(mv3d_startup_tests PROPERTIES TIMEOUT 30)
        list(APPEND PCV_APPLICATION_TEST_TARGETS mv3d_startup_tests)
    endif()
    add_executable(capture_pose_tests unit/camera/capture_pose_tests.cpp)
    target_include_directories(capture_pose_tests PRIVATE ${PROJECT_SOURCE_DIR}/modules/10_pointcloud_io/src)
    target_link_libraries(capture_pose_tests PRIVATE pcv_m10_pointcloud_io pcv_m30_pointcloud_stitch pcv_m50_coordinate_conversion)
    add_test(NAME capture_pose_tests COMMAND capture_pose_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS capture_pose_tests)
    add_executable(capture_sdk_frame_tests unit/camera/capture_sdk_frame_tests.cpp)
    target_include_directories(capture_sdk_frame_tests PRIVATE ${PROJECT_SOURCE_DIR}/modules/10_pointcloud_io/src)
    target_link_libraries(capture_sdk_frame_tests PRIVATE pcv_m10_pointcloud_io pcv_m30_pointcloud_stitch pcv_m50_coordinate_conversion)
    add_test(NAME capture_sdk_frame_tests COMMAND capture_sdk_frame_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS capture_sdk_frame_tests)
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

add_executable(application_config_tests unit/core/application_config_tests.cpp)
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

add_executable(plane_output_tests unit/io/plane_output_tests.cpp)
target_link_libraries(plane_output_tests PRIVATE Qt::Core Qt::Gui pcv_m10_pointcloud_io)
add_test(NAME plane_output_tests COMMAND plane_output_tests)

add_executable(plane_mask_tests unit/geometry/plane_mask_tests.cpp)
target_link_libraries(plane_mask_tests PRIVATE Qt::Core Qt::Gui pcv_m60_geometry_workpiece)
add_test(NAME plane_mask_tests COMMAND plane_mask_tests)

add_executable(temp_workpiece_interface_tests unit/geometry/temp_workpiece_interface_tests.cpp)
target_link_libraries(temp_workpiece_interface_tests PRIVATE
    Qt::Core Qt::Gui pcv_m60_geometry_workpiece pcv_m50_coordinate_conversion)
if(TARGET pcv_m30_pointcloud_stitch)
    # Cross-path check: the scanning JSON entry and the multi-frame entry must
    # return identical robot_base coordinates for the same PLY/XML/poses.
    target_link_libraries(temp_workpiece_interface_tests PRIVATE pcv_m30_pointcloud_stitch)
    target_compile_definitions(temp_workpiece_interface_tests PRIVATE PCV_TEST_HAS_M30_STITCHING=1)
endif()
add_test(NAME temp_workpiece_interface_tests COMMAND temp_workpiece_interface_tests)

add_executable(cad_template_tests unit/geometry/cad_template_tests.cpp)
target_link_libraries(cad_template_tests PRIVATE pcv_m60_geometry_workpiece)
if(PCV_BUILD_STEP_IMPORT)
    target_include_directories(cad_template_tests PRIVATE ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(cad_template_tests PRIVATE TKPrim TKBO TKDESTEP TKXCAF)
endif()
if(WIN32)
    target_link_libraries(cad_template_tests PRIVATE Psapi)
endif()
add_test(NAME cad_template_tests COMMAND cad_template_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS cad_template_tests)

add_executable(workpiece_template_tests unit/geometry/workpiece_template_tests.cpp)
target_link_libraries(workpiece_template_tests PRIVATE pcv_m60_geometry_workpiece)
target_compile_definitions(workpiece_template_tests PRIVATE PCV_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
if(WIN32)
    target_link_libraries(workpiece_template_tests PRIVATE Psapi)
endif()
add_test(NAME workpiece_template_tests COMMAND workpiece_template_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS workpiece_template_tests)

if(TARGET pcv_m30_pointcloud_stitch)
    add_executable(stitching_interface_tests unit/stitching/stitching_interface_tests.cpp)
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
list(APPEND PCV_APPLICATION_TEST_TARGETS plane_output_tests plane_mask_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS temp_workpiece_interface_tests)
if(TARGET stitching_interface_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS stitching_interface_tests)
endif()

if(PCV_BUILD_POINTCLOUDVIEW AND PCV_BUILD_EXPERIMENTAL_CAMERA)
    add_executable(camera_depth_preview_tests
        gui/pointcloudview/camera_depth_preview_tests.cpp
        ../apps/pointcloudview/process_memory_usage_v1.cpp
        ../apps/pointcloudview/features/camera_depth_preview_v1.cpp)
    target_include_directories(camera_depth_preview_tests PRIVATE ../apps/pointcloudview)
    target_compile_definitions(camera_depth_preview_tests PRIVATE PCV_CAPTURE_UI_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
    target_link_libraries(camera_depth_preview_tests PRIVATE Qt::Widgets Qt::Concurrent pcv_m20_pointcloud_render pcv_m60_geometry_workpiece)
    if(WIN32)
        target_link_libraries(camera_depth_preview_tests PRIVATE Psapi)
    endif()
    add_test(NAME camera_depth_preview_tests COMMAND camera_depth_preview_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS camera_depth_preview_tests)
    add_executable(camera_capture_panel_tests
        gui/pointcloudview/camera_capture_panel_tests.cpp
        ../apps/pointcloudview/features/camera_capture_panel_v1.cpp
        ../apps/pointcloudview/cameradialog.cpp
        ../apps/pointcloudview/features/camera_depth_preview_v1.cpp
        ../apps/pointcloudview/cameradialog.h
        ../apps/pointcloudview/live_pipeline.cpp
        ../apps/pointcloudview/ui/cameradialog.ui)
    set_property(TARGET camera_capture_panel_tests PROPERTY AUTOUIC_SEARCH_PATHS
        "${PROJECT_SOURCE_DIR}/apps/pointcloudview/ui")
    target_include_directories(camera_capture_panel_tests PRIVATE ../apps/pointcloudview)
    target_compile_definitions(camera_capture_panel_tests PRIVATE PCV_CAPTURE_UI_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
    target_link_libraries(camera_capture_panel_tests PRIVATE pcv_selected_plane_tasks Qt::Widgets Qt::Concurrent
        pcv_m30_pointcloud_stitch pcv_m60_geometry_workpiece pcv_m20_pointcloud_render)
    add_test(NAME camera_capture_panel_tests COMMAND camera_capture_panel_tests)
    set_tests_properties(camera_capture_panel_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    list(APPEND PCV_APPLICATION_TEST_TARGETS camera_capture_panel_tests)
    add_executable(live_pipeline_tests
        unit/applications/live_pipeline_tests.cpp
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
        unit/applications/pointcloudprocessor_obstacle_tests.cpp
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

add_executable(handeye_transform_tests unit/coordinates/handeye_transform_tests.cpp)
target_link_libraries(handeye_transform_tests PRIVATE Qt::Core Qt::Gui pcv_m50_coordinate_conversion)
add_test(NAME handeye_transform_tests COMMAND handeye_transform_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS handeye_transform_tests)

add_executable(roi_matching_tests unit/geometry/roi_matching_tests.cpp)
target_link_libraries(roi_matching_tests PRIVATE
    Qt::Core Qt::Gui pcv_m60_geometry_workpiece pcv_m50_coordinate_conversion)
add_test(NAME roi_matching_tests COMMAND roi_matching_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS roi_matching_tests)

if(TARGET pcv_m30_pointcloud_stitch)
    add_executable(seam_fusion_tests unit/stitching/seam_fusion_tests.cpp)
    target_link_libraries(seam_fusion_tests PRIVATE Qt::Core Qt::Gui pcv_m30_pointcloud_stitch)
    add_test(NAME seam_fusion_tests COMMAND seam_fusion_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS seam_fusion_tests)
endif()

add_executable(multiframe_registration_tests unit/stitching/multiframe_registration_tests.cpp)
target_link_libraries(multiframe_registration_tests PRIVATE Qt::Core Qt::Gui pcv_m30_pointcloud_stitch)
add_test(NAME multiframe_registration_tests COMMAND multiframe_registration_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS multiframe_registration_tests)

add_executable(selected_plane_stitch_tests unit/stitching/selected_plane_stitch_tests.cpp)
target_link_libraries(selected_plane_stitch_tests PRIVATE Qt::Core Qt::Gui pcv_m30_pointcloud_stitch pcv_m60_geometry_workpiece)
add_test(NAME selected_plane_stitch_tests COMMAND selected_plane_stitch_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS selected_plane_stitch_tests)

add_executable(plane_fitting_tests unit/geometry/plane_fitting_tests.cpp)
target_link_libraries(plane_fitting_tests PRIVATE Qt::Core Qt::Gui pcv_m60_geometry_workpiece)
add_test(NAME plane_fitting_tests COMMAND plane_fitting_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS plane_fitting_tests)

add_executable(single_click_plane_selection_tests unit/geometry/single_click_plane_selection_tests.cpp)
target_link_libraries(single_click_plane_selection_tests PRIVATE Qt::Core Qt::Gui pcv_m60_geometry_workpiece)
add_test(NAME single_click_plane_selection_tests COMMAND single_click_plane_selection_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS single_click_plane_selection_tests)

add_executable(plane_fitting_equivalence_tests unit/geometry/plane_fitting_equivalence_tests.cpp)
target_link_libraries(plane_fitting_equivalence_tests PRIVATE Qt::Core Qt::Gui pcv_m60_geometry_workpiece)
add_test(NAME plane_fitting_equivalence_tests COMMAND plane_fitting_equivalence_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS plane_fitting_equivalence_tests)

add_executable(scan_planner_tests unit/geometry/scan_planner_tests.cpp)
target_link_libraries(scan_planner_tests PRIVATE Qt::Core Qt::Gui pcv_m60_geometry_workpiece)
add_test(NAME scan_planner_tests COMMAND scan_planner_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS scan_planner_tests)

add_executable(texture_mapping_tests unit/geometry/texture_mapping_tests.cpp)
target_link_libraries(texture_mapping_tests PRIVATE Qt::Core Qt::Gui pcv_m60_geometry_workpiece)
add_test(NAME texture_mapping_tests COMMAND texture_mapping_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS texture_mapping_tests)

if(PCV_BUILD_POINTCLOUDVIEW)
    add_executable(cad_import_view_tests
        gui/pointcloudview/cad_import_view_tests.cpp
        ../apps/pointcloudview/features/cad_import_dialog_v1.cpp)
    target_include_directories(cad_import_view_tests PRIVATE ../apps/pointcloudview)
    target_link_libraries(cad_import_view_tests PRIVATE pcv_m20_pointcloud_render pcv_m60_geometry_workpiece Qt::Concurrent)
    if(PCV_BUILD_STEP_IMPORT)
        target_include_directories(cad_import_view_tests PRIVATE ${OpenCASCADE_INCLUDE_DIR})
        target_link_libraries(cad_import_view_tests PRIVATE TKPrim TKDESTEP)
    endif()
    add_test(NAME cad_import_view_tests COMMAND cad_import_view_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS cad_import_view_tests)
    add_executable(workpiece_template_view_tests gui/pointcloudview/workpiece_template_view_tests.cpp)
    target_link_libraries(workpiece_template_view_tests PRIVATE pcv_workbench)
    add_test(NAME workpiece_template_view_tests COMMAND workpiece_template_view_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS workpiece_template_view_tests)
    add_executable(selected_plane_dialog_tests
        gui/pointcloudview/selected_plane_dialog_tests.cpp
        ../apps/pointcloudview/selectedplanedialog.cpp
        ../apps/pointcloudview/selectedplanedialog.h
        ../apps/pointcloudview/features/workpiece_template_dialog_v1.cpp
        ../apps/pointcloudview/features/workpiece_template_dialog_v1.h)
    target_include_directories(selected_plane_dialog_tests PRIVATE ../apps/pointcloudview)
    target_link_libraries(selected_plane_dialog_tests PRIVATE Qt::Core Qt::Gui Qt::Widgets Qt::Concurrent
        Qt::OpenGL Qt::OpenGLWidgets pcv_m20_pointcloud_render pcv_m30_pointcloud_stitch pcv_m60_geometry_workpiece pcv_selected_plane_tasks)
    add_test(NAME selected_plane_dialog_tests COMMAND selected_plane_dialog_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS selected_plane_dialog_tests)
    set_tests_properties(selected_plane_dialog_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    add_executable(texture_experiment_tests
        unit/applications/texture_experiment_tests.cpp
    )
    target_link_libraries(texture_experiment_tests PRIVATE
        Qt::Core Qt::Gui pcv_m10_pointcloud_io pcv_m60_geometry_workpiece)
    add_test(NAME texture_experiment_tests COMMAND texture_experiment_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS texture_experiment_tests)
endif()

if(TARGET pcv_m20_pointcloud_render)
    add_executable(pointcloud_canvas_contract_tests unit/rendering/pointcloud_canvas_contract_tests.cpp)
    target_link_libraries(pointcloud_canvas_contract_tests PRIVATE Qt::Core Qt::Gui pcv_m20_pointcloud_render)
    target_include_directories(pointcloud_canvas_contract_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/modules/20_pointcloud_render/src)
    add_test(NAME pointcloud_canvas_contract_tests COMMAND pointcloud_canvas_contract_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS pointcloud_canvas_contract_tests)
endif()

if(TARGET pcv_stitching_ui)
    add_executable(stitching_ui_tests gui/pointcloudview/stitching_ui_tests.cpp)
    target_link_libraries(stitching_ui_tests PRIVATE pcv_stitching_ui)
    add_test(NAME stitching_ui_tests COMMAND stitching_ui_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS stitching_ui_tests)
    if(PCV_BUILD_POINTCLOUDVIEW)
        add_executable(stitching_view_tests gui/pointcloudview/stitching_ui_tests.cpp)
    target_link_libraries(stitching_view_tests PRIVATE pcv_workbench)
    target_compile_definitions(stitching_view_tests PRIVATE PCV_TEST_STITCHING_VIEW=1)
    add_test(NAME stitching_view_tests COMMAND stitching_view_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS stitching_view_tests)
    endif()
endif()

if(PCV_BUILD_POINTCLOUDVIEW)
    add_executable(process_memory_usage_tests
        unit/applications/process_memory_usage_tests.cpp
        ../apps/pointcloudview/process_memory_usage_v1.cpp
        ../apps/pointcloudview/process_memory_usage_v1.h)
    target_include_directories(process_memory_usage_tests PRIVATE ../apps/pointcloudview)
    target_link_libraries(process_memory_usage_tests PRIVATE Qt::Core)
    if(WIN32)
        target_link_libraries(process_memory_usage_tests PRIVATE Psapi)
    endif()
    add_test(NAME process_memory_usage_tests COMMAND process_memory_usage_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS process_memory_usage_tests)
endif()

add_executable(step_sampled_template_tests unit/geometry/step_sampled_template_tests.cpp)
target_link_libraries(step_sampled_template_tests PRIVATE pcv_m60_geometry_workpiece)
add_test(NAME step_sampled_template_tests COMMAND step_sampled_template_tests)
list(APPEND PCV_APPLICATION_TEST_TARGETS step_sampled_template_tests)

if(TARGET pcv_step_template_builder_view_v1)
    add_executable(step_template_builder_session_tests
        unit/applications/step_template_builder_session_tests.cpp)
    target_link_libraries(step_template_builder_session_tests PRIVATE pcv_step_template_builder_view_v1)
    add_test(NAME step_template_builder_session_tests COMMAND step_template_builder_session_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS step_template_builder_session_tests)
    set_tests_properties(step_template_builder_session_tests PROPERTIES TIMEOUT 90)
    add_executable(step_template_builder_contract_tests
        gui/step_template_builder/step_template_builder_contract_tests.cpp)
    target_link_libraries(step_template_builder_contract_tests PRIVATE
        pcv_step_template_builder_view_v1)
    target_compile_definitions(step_template_builder_contract_tests PRIVATE
        $<$<BOOL:${PCV_BUILD_STEP_IMPORT}>:PCV_BUILD_STEP_IMPORT=1>)
    add_test(NAME step_template_builder_contract_tests
        COMMAND step_template_builder_contract_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS step_template_builder_contract_tests)
endif()

if(PCV_BUILD_SCAN_GEOMETRY_DEMO_V1)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME scan_geometry_log_summary_tests
            COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/unit/applications/scan_geometry_log_summary_tests.py")
        set_tests_properties(scan_geometry_log_summary_tests PROPERTIES TIMEOUT 30)
    endif()
    add_executable(scan_geometry_contract_tests unit/applications/scan_geometry_contract_tests.cpp)
    target_link_libraries(scan_geometry_contract_tests PRIVATE pcv_scan_geometry_session_v2 Qt::Test)
    add_test(NAME scan_geometry_contract_tests COMMAND scan_geometry_contract_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS scan_geometry_contract_tests)
    add_executable(scan_geometry_session_tests unit/applications/scan_geometry_session_tests.cpp)
    add_executable(result_regions_tests unit/applications/result_regions_tests.cpp)
    target_link_libraries(result_regions_tests PRIVATE pcv_scan_geometry_session_v2)
    add_test(NAME result_regions_tests COMMAND result_regions_tests)
    list(APPEND PCV_APPLICATION_TEST_TARGETS result_regions_tests)
    target_link_libraries(scan_geometry_session_tests PRIVATE pcv_scan_geometry_session_v2 Qt::Test)
    add_test(NAME scan_geometry_session_tests COMMAND scan_geometry_session_tests)
    add_executable(scan_geometry_view_tests gui/scan_geometry/scan_geometry_view_tests.cpp)
    target_link_libraries(scan_geometry_view_tests PRIVATE pcv_scan_geometry_demo_view_v1 Qt::Test)
    add_test(NAME scan_geometry_view_tests COMMAND scan_geometry_view_tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/scan_geometry_view_tests.txt,txt")
    add_executable(reference_plane_match_tests unit/geometry/reference_plane_match_tests.cpp)
    target_link_libraries(reference_plane_match_tests PRIVATE pcv_selected_plane_tasks Qt::Test)
    add_test(NAME reference_plane_match_tests COMMAND reference_plane_match_tests)
    add_executable(high_side_plane_candidates_tests unit/geometry/high_side_plane_candidates_tests.cpp)
    target_link_libraries(high_side_plane_candidates_tests PRIVATE pcv_m60_geometry_workpiece Qt::Test)
    add_test(NAME high_side_plane_candidates_tests COMMAND high_side_plane_candidates_tests)
    set_tests_properties(scan_geometry_session_tests reference_plane_match_tests high_side_plane_candidates_tests PROPERTIES TIMEOUT 180)
    list(APPEND PCV_APPLICATION_TEST_TARGETS scan_geometry_session_tests scan_geometry_view_tests reference_plane_match_tests high_side_plane_candidates_tests)
endif()

if(TARGET pcv_m40_pointcloud_matching)
    add_executable(template_match_tests unit/matching/template_match_tests.cpp)
    target_link_libraries(template_match_tests PRIVATE pcv_m40_pointcloud_matching)
    add_test(NAME template_match_tests COMMAND template_match_tests)
    set_tests_properties(template_match_tests PROPERTIES TIMEOUT 240)
    list(APPEND PCV_APPLICATION_TEST_TARGETS template_match_tests)
endif()

if(WIN32 AND TARGET Qt6::Core AND PCV_APPLICATION_TEST_TARGETS)
    set(PCV_TEST_RUNTIME_PATH "$<TARGET_FILE_DIR:Qt6::Core>")
    if(PCV_BUILD_EXPERIMENTAL_CAMERA)
        foreach(cameraRuntime
            "C:/Program Files (x86)/Common Files/Mv3dLpSDK/Runtime/Win64_x64"
            "C:/Program Files (x86)/Common Files/MV3D/Runtime/Win64_x64")
            if(EXISTS "${cameraRuntime}")
                string(APPEND PCV_TEST_RUNTIME_PATH ";${cameraRuntime}")
            endif()
        endforeach()
    endif()
    # ENVIRONMENT is a CMake list: PATH semicolons must stay inside one entry.
    string(REPLACE ";" "\\;" PCV_TEST_PATH "PATH=${PCV_TEST_RUNTIME_PATH};$ENV{PATH}")
    set_tests_properties(${PCV_APPLICATION_TEST_TARGETS}
        PROPERTIES ENVIRONMENT "${PCV_TEST_PATH}")
endif()
if(TARGET scan_geometry_view_tests)
    set_tests_properties(scan_geometry_view_tests PROPERTIES TIMEOUT 90)
    set_property(TEST scan_geometry_view_tests APPEND PROPERTY ENVIRONMENT
        "QT_QPA_PLATFORM=windows"
        "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
        "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms")
endif()
foreach(stitchTest step_template_builder_contract_tests cad_import_view_tests stitching_ui_tests stitching_view_tests workpiece_template_view_tests camera_capture_panel_tests camera_depth_preview_tests)
    if(TARGET ${stitchTest})
        set_property(TEST ${stitchTest} APPEND PROPERTY ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
            "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms")
        set_tests_properties(${stitchTest} PROPERTIES TIMEOUT 90)
    endif()
endforeach()
if(TARGET texture_experiment_tests)
    set_property(TEST texture_experiment_tests APPEND PROPERTY ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(texture_experiment_tests PROPERTIES TIMEOUT 30)
endif()
if(TARGET selected_plane_dialog_tests)
    set_property(TEST selected_plane_dialog_tests APPEND PROPERTY ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen"
        "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
        "QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms")
endif()

foreach(cad_runtime_test IN LISTS PCV_APPLICATION_TEST_TARGETS)
    pcv_deploy_step_runtime(${cad_runtime_test})
endforeach()

foreach(pcv_test IN LISTS PCV_APPLICATION_TEST_TARGETS)
    get_target_property(pcv_sources ${pcv_test} SOURCES)
    if(pcv_sources MATCHES "experimental/")
        set_tests_properties(${pcv_test} PROPERTIES LABELS "experimental;camera")
    elseif(pcv_sources MATCHES "gui/")
        set_tests_properties(${pcv_test} PROPERTIES LABELS "gui")
    elseif(pcv_sources MATCHES "unit/applications/")
        set_tests_properties(${pcv_test} PROPERTIES LABELS "application")
    else()
        set_tests_properties(${pcv_test} PROPERTIES LABELS "unit")
    endif()
endforeach()

if(TARGET pcv_selected_plane_tasks)
    add_executable(task_runtime_tests unit/applications/task_runtime_tests.cpp)
    target_link_libraries(task_runtime_tests PRIVATE pcv_selected_plane_tasks)
    add_test(NAME task_runtime_tests COMMAND task_runtime_tests)
    set_tests_properties(task_runtime_tests PROPERTIES LABELS application)
    if(WIN32)
        set_tests_properties(task_runtime_tests PROPERTIES ENVIRONMENT "${PCV_TEST_PATH}")
    endif()
endif()

# Launch the actual entrypoints on the native desktop and leave through their
# event loops. These are process/resource smoke checks, not device acceptance.
foreach(pcv_smoke IN ITEMS pointcloudview scan_geometry_demo_v1 step_template_builder_v1)
    if(TARGET ${pcv_smoke})
        add_test(NAME ${pcv_smoke}_startup COMMAND ${pcv_smoke})
        set_tests_properties(${pcv_smoke}_startup PROPERTIES TIMEOUT 30 LABELS "gui;smoke")
        set_property(TEST ${pcv_smoke}_startup APPEND PROPERTY ENVIRONMENT
            "${PCV_TEST_PATH}"
            "QT_QPA_PLATFORM=windows"
            "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
            "POINTCLOUDVIEW_SELFTEST_CLOSE=1"
            "PCV_SCAN_GEOMETRY_SELFTEST_CLOSE=1"
            "PCV_STEP_TEMPLATE_SELFTEST_CLOSE=1")
    endif()
endforeach()
