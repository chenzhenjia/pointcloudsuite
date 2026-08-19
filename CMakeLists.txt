cmake_minimum_required(VERSION 3.19)
project(PointCloudSuite VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PCV_BUILD_POINTCLOUDVIEW "Build the point cloud viewer" ON)
option(PCV_BUILD_POINTCLOUDSTITCH "Build the point cloud stitching application" ON)
option(PCV_BUILD_TOOLS "Build diagnostic command-line tools" OFF)
option(PCV_BUILD_TESTS "Build all automated tests" OFF)

if(WIN32)
    string(REGEX MATCH "[^ -~]" PCV_SOURCE_PATH_NON_ASCII "${CMAKE_SOURCE_DIR}")
    string(REGEX MATCH "[^ -~]" PCV_BINARY_PATH_NON_ASCII "${CMAKE_BINARY_DIR}")
    if(PCV_SOURCE_PATH_NON_ASCII OR PCV_BINARY_PATH_NON_ASCII)
        message(FATAL_ERROR
            "Qt Windows builds require ASCII-only source and build paths.\n"
            "Source: ${CMAKE_SOURCE_DIR}\nBuild: ${CMAKE_BINARY_DIR}")
    endif()
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core Gui Widgets Concurrent OpenGL OpenGLWidgets)
qt_standard_project_setup()

if(PCV_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(src)

if(PCV_BUILD_POINTCLOUDVIEW)
    add_subdirectory(apps/pointcloudview)
endif()
if(PCV_BUILD_POINTCLOUDSTITCH)
    add_subdirectory(apps/pointcloudstitch)
endif()
if(PCV_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
if(PCV_BUILD_TESTS)
    add_subdirectory(tests)
endif()
