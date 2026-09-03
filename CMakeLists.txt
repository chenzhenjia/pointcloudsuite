cmake_minimum_required(VERSION 3.19)

# Force the English /showIncludes prefix while CMake detects MSVC. The same
# environment is applied to every compile through the launcher below and by
# scripts/build_windows.ps1, so the dependency database stays usable.
if(CMAKE_HOST_WIN32)
    set(ENV{VSLANG} 1033)
endif()

if(CMAKE_GENERATOR MATCHES "NMake Makefiles JOM")
    message(
        "NMake Makefiles JOM is unsupported because it records localized MSVC /showIncludes output as dependency names. "
        "Select the NMake Makefiles generator and configure a fresh build directory.")
endif()

project(PointCloudSuite VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PCV_BUILD_POINTCLOUDVIEW "Build the point cloud viewer" ON)
option(PCV_BUILD_POINTCLOUDSTITCH "Build the point cloud stitching application" ON)
option(PCV_BUILD_TOOLS "Build diagnostic command-line tools" OFF)
option(PCV_BUILD_TESTS "Build all automated tests" OFF)
option(PCV_BUILD_EXPERIMENTAL_CAMERA
    "Build Hikvision camera acquisition and its UI/tests" ON)

if(MSVC)
    set(CMAKE_CXX_COMPILER_LAUNCHER
        "${CMAKE_COMMAND};-E;env;VSLANG=1033")
    if(NOT CMAKE_CXX_ABI_COMPILED OR NOT CMAKE_CXX_SIZEOF_DATA_PTR STREQUAL "8")
        message(FATAL_ERROR
            "MSVC ABI detection failed. CMAKE_CXX_ABI_COMPILED=${CMAKE_CXX_ABI_COMPILED}, "
            "CMAKE_CXX_SIZEOF_DATA_PTR=${CMAKE_CXX_SIZEOF_DATA_PTR}. Configure with VSLANG=1033.")
    endif()
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core Gui Widgets Concurrent OpenGL OpenGLWidgets)
qt_standard_project_setup()

if(PCV_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(src)
add_subdirectory(modules)

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
