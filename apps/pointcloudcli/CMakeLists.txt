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
endif()
