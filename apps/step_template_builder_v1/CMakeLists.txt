add_library(pcv_step_template_builder_view_v1 STATIC
    step_template_builder_session_v1.cpp
    step_template_builder_session_v1.h
    step_template_builder_window_v1.cpp
    step_template_builder_window_v1.h
    step_template_builder_window_v1.ui
    step_template_builder_v1.qrc)
set_target_properties(pcv_step_template_builder_view_v1 PROPERTIES AUTOUIC ON AUTOMOC ON AUTORCC ON)
target_include_directories(pcv_step_template_builder_view_v1 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pcv_step_template_builder_view_v1 PUBLIC
    pcv_m20_pointcloud_render pcv_m60_geometry_workpiece Qt::Concurrent Qt::Widgets)
target_compile_definitions(pcv_step_template_builder_view_v1 PRIVATE
    PCV_STEP_TEMPLATE_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")

qt_add_executable(step_template_builder_v1 WIN32 main.cpp)
target_link_libraries(step_template_builder_v1 PRIVATE pcv_step_template_builder_view_v1)
pcv_deploy_step_runtime(step_template_builder_v1)
if(WIN32)
    add_custom_command(TARGET step_template_builder_v1 POST_BUILD
        COMMAND Qt6::windeployqt --$<IF:$<CONFIG:Debug>,debug,release>
            --compiler-runtime --no-translations "$<TARGET_FILE:step_template_builder_v1>"
        VERBATIM)
endif()
