add_library(pcv_scan_geometry_session_v2 STATIC scan_geometry_session_v2.cpp scan_geometry_session_v2.h
    scan_geometry_output_v1.cpp scan_geometry_output_v1.h
    scan_geometry_contract_v1.cpp scan_geometry_contract_v1.h
    result_regions_v1.cpp result_regions_v1.h)
set_target_properties(pcv_scan_geometry_session_v2 PROPERTIES AUTOMOC ON)
target_include_directories(pcv_scan_geometry_session_v2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pcv_scan_geometry_session_v2 PUBLIC pcv_selected_plane_tasks_v2 pcv_m60_geometry_workpiece pcv_m10_pointcloud_io)
target_compile_definitions(pcv_scan_geometry_session_v2 PRIVATE PCV_SCAN_GEOMETRY_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")

add_library(pcv_scan_geometry_demo_view_v1 STATIC
    scan_geometry_canvas_v2.cpp scan_geometry_canvas_v2.h scan_geometry_window_v1.cpp scan_geometry_window_v1.h
    input_config_dialog_v2.cpp input_config_dialog_v2.h input_config_dialog_v2.ui
    scan_geometry_window_v1.ui scan_geometry_v1.qrc)
set_target_properties(pcv_scan_geometry_demo_view_v1 PROPERTIES AUTOUIC ON AUTOMOC ON AUTORCC ON)
target_link_libraries(pcv_scan_geometry_demo_view_v1 PUBLIC pcv_scan_geometry_session_v2 pcv_m20_pointcloud_render)


qt_add_executable(scan_geometry_demo_v1 WIN32 main.cpp)
target_link_libraries(scan_geometry_demo_v1 PRIVATE pcv_scan_geometry_demo_view_v1)
pcv_deploy_step_runtime(scan_geometry_demo_v1)
if(WIN32)
    add_custom_command(TARGET scan_geometry_demo_v1 POST_BUILD
        COMMAND Qt6::windeployqt --$<IF:$<CONFIG:Debug>,debug,release>
            --compiler-runtime --no-translations "$<TARGET_FILE:scan_geometry_demo_v1>"
        VERBATIM)
endif()
