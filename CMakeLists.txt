cmake_minimum_required(VERSION 3.19)
project(pointcloudview LANGUAGES CXX)

option(POINTCLOUDVIEW_BUILD_TESTS "Build pointcloud processor tests" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt's Windows toolchain (especially moc/uic/rcc and older JOM/NMake
# combinations) may fail when either the source or build path contains
# non-ASCII characters. Fail early with a clear, actionable message instead
# of producing a cryptic compiler error deep in the generated build files.
if (WIN32)
    string(REGEX MATCH "[^ -~]" _source_non_ascii "${CMAKE_SOURCE_DIR}")
    string(REGEX MATCH "[^ -~]" _binary_non_ascii "${CMAKE_BINARY_DIR}")
    if (_source_non_ascii OR _binary_non_ascii)
        message(FATAL_ERROR
            "Qt Windows builds require ASCII-only source and build paths.\n"
            "Source: ${CMAKE_SOURCE_DIR}\n"
            "Build:  ${CMAKE_BINARY_DIR}\n"
            "Move/clone the project to a path such as C:/work/pointcloudview "
            "and configure into C:/work/pointcloudview/build.")
    endif()
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Concurrent OpenGL OpenGLWidgets)

qt_standard_project_setup()

qt_add_executable(pointcloudview
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    pointcloudprocessor.cpp
    pointcloudprocessor.h
)

target_link_libraries(pointcloudview
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Concurrent
        Qt::OpenGL
        Qt::OpenGLWidgets
)

if (WIN32)
    add_custom_command(TARGET pointcloudview POST_BUILD
        COMMAND Qt6::windeployqt
            --$<IF:$<CONFIG:Debug>,debug,release>
            --compiler-runtime
            --no-translations
            "$<TARGET_FILE:pointcloudview>"
        COMMENT "Deploying Qt runtime next to pointcloudview.exe"
        VERBATIM
    )
endif()

if (POINTCLOUDVIEW_BUILD_TESTS)
    enable_testing()
    qt_add_executable(pointcloudprocessor_tests
        tests/pointcloudprocessor_tests.cpp
        pointcloudprocessor.cpp
        pointcloudprocessor.h
    )
    target_include_directories(pointcloudprocessor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(pointcloudprocessor_tests PRIVATE Qt::Core Qt::Gui)
    add_test(NAME pointcloudprocessor_tests COMMAND pointcloudprocessor_tests)
    add_executable(registration_diagnostic_runner registration_diagnostic_runner.cpp pointcloudprocessor.cpp pointcloudprocessor.h)
    target_link_libraries(registration_diagnostic_runner PRIVATE Qt::Core Qt::Gui)
    if (WIN32)
        set_tests_properties(pointcloudprocessor_tests PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
    endif()
endif()

install(TARGETS pointcloudview
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET pointcloudview
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
