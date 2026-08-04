cmake_minimum_required(VERSION 3.19)
project(pointcloudciew LANGUAGES CXX)

include(CTest)

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

qt_add_executable(pointcloudciew
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    pointcloudprocessor.cpp
    pointcloudprocessor.h
)

target_link_libraries(pointcloudciew
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Concurrent
        Qt::OpenGL
        Qt::OpenGLWidgets
)

if (BUILD_TESTING)
    qt_add_executable(pointcloudprocessor_tests
        tests/pointcloudprocessor_tests.cpp
        pointcloudprocessor.cpp
        pointcloudprocessor.h
    )
    target_include_directories(pointcloudprocessor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(pointcloudprocessor_tests PRIVATE Qt::Core)
    add_test(NAME pointcloudprocessor_tests COMMAND pointcloudprocessor_tests)
    if (WIN32)
        set_tests_properties(pointcloudprocessor_tests PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
    endif()
endif()

install(TARGETS pointcloudciew
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET pointcloudciew
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
