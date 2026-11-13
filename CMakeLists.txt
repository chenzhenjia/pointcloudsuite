cmake_minimum_required(VERSION 3.19)

# Force the English /showIncludes prefix while CMake detects MSVC. The same
# environment is applied to every compile through the launcher below and by
# scripts/build_windows.ps1, so the dependency database stays usable.
if(CMAKE_HOST_WIN32)
    set(ENV{VSLANG} 1033)
endif()

if(CMAKE_GENERATOR MATCHES "NMake Makefiles JOM")
    message(FATAL_ERROR
        "NMake Makefiles JOM is unsupported because it records localized MSVC /showIncludes output as dependency names. "
        "Select the NMake Makefiles generator and configure a fresh build directory.")
endif()

project(PointCloudSuite VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The maintained desktop entrypoints can also be configured independently.
option(PCV_BUILD_POINTCLOUDVIEW "Build the point cloud viewer" ON)
option(PCV_BUILD_SCAN_GEOMETRY_DEMO_V1 "Build the offline PLY selected-plane GUI (legacy target name)" ON)
option(PCV_BUILD_STEP_TEMPLATE_BUILDER_V1 "Build the standalone STEP template builder GUI" ON)
option(PCV_BUILD_RAW_CONVERTER "Build the depth RAW to PLY converter" OFF)
option(PCV_BUILD_STEP_IMPORT "Enable OCCT STEP import" ON)
option(PCV_BUILD_TESTS "Build all automated tests" OFF)
option(PCV_BUILD_EXPERIMENTAL_TESTS "Build optional SDK-specific experimental tests" OFF)
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

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui)
if(PCV_BUILD_POINTCLOUDVIEW OR PCV_BUILD_RAW_CONVERTER
   OR PCV_BUILD_TESTS OR PCV_BUILD_SCAN_GEOMETRY_DEMO_V1 OR PCV_BUILD_STEP_TEMPLATE_BUILDER_V1)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Concurrent)
endif()
if(PCV_BUILD_POINTCLOUDVIEW OR PCV_BUILD_SCAN_GEOMETRY_DEMO_V1 OR PCV_BUILD_STEP_TEMPLATE_BUILDER_V1)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
endif()
qt_standard_project_setup()

if(PCV_BUILD_TESTS)
    enable_testing()
endif()

if(PCV_BUILD_STEP_IMPORT)
    find_package(OpenCASCADE 7.9.3 EXACT REQUIRED COMPONENTS FoundationClasses ModelingData ModelingAlgorithms Visualization ApplicationFramework DataExchange)
endif()

include(cmake/step_runtime_v1.cmake)

add_subdirectory(src)
add_subdirectory(modules)
if(PCV_BUILD_POINTCLOUDVIEW OR PCV_BUILD_SCAN_GEOMETRY_DEMO_V1)
    add_subdirectory(apps/stitching_ui)
endif()

# Keep the desktop applications directly connected to the root project. This
# makes both executable targets visible to Qt Creator after CMake reconfigure.
if(PCV_BUILD_POINTCLOUDVIEW)
    add_subdirectory(apps/pointcloudview)
endif()
if(PCV_BUILD_SCAN_GEOMETRY_DEMO_V1)
    add_subdirectory(apps/scan_geometry_demo_v1)
endif()
if(PCV_BUILD_STEP_TEMPLATE_BUILDER_V1)
    add_subdirectory(apps/step_template_builder_v1)
endif()
if(PCV_BUILD_RAW_CONVERTER)
    add_subdirectory(tools)
endif()
if(PCV_BUILD_TESTS)
    add_subdirectory(tests)
endif()
