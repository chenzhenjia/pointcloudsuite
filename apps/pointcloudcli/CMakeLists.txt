add_executable(pointcloudcli
    main.cpp
)

set_target_properties(pointcloudcli PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_compile_definitions(pointcloudcli PRIVATE
    PCV_PROJECT_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(pointcloudcli PRIVATE
    Qt::Core Qt::Gui Qt::Concurrent
    pcv_core pcv_infrastructure pcv_filtering
    pcv_m10_pointcloud_read
    pcv_m40_pointcloud_matching
    pcv_m50_coordinate_conversion
    pcv_m60_plane_fitting
    pcv_m70_roi_template
    pcv_m80_plane_output)

if(PCV_BUILD_TESTS)
    foreach(PCV_CLI_TEST_MODE IN ITEMS help version empty unknown)
        add_test(
            NAME cli_${PCV_CLI_TEST_MODE}
            COMMAND ${CMAKE_COMMAND}
                "-DPCV_CLI=$<TARGET_FILE:pointcloudcli>"
                "-DPCV_MODE=${PCV_CLI_TEST_MODE}"
                "-DPCV_PROJECT_VERSION=${PROJECT_VERSION}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_cli_invocation.cmake"
        )
    endforeach()
    foreach(PCV_CLI_JOB_MODE IN ITEMS rectangle indices overwrite traversal)
        add_test(
            NAME cli_temp_workpiece_${PCV_CLI_JOB_MODE}
            COMMAND ${CMAKE_COMMAND}
                "-DPCV_CLI=$<TARGET_FILE:pointcloudcli>"
                "-DPCV_MODE=${PCV_CLI_JOB_MODE}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_cli_job.cmake"
        )
    endforeach()
    if(WIN32 AND TARGET Qt6::Core)
        set_tests_properties(cli_help cli_version cli_empty cli_unknown
            cli_temp_workpiece_rectangle cli_temp_workpiece_indices
            cli_temp_workpiece_overwrite cli_temp_workpiece_traversal PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
    endif()
endif()
