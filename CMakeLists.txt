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

# Keep these cache variables for compatibility with existing local CMake
# configurations. The two desktop applications are the fixed root entrypoints
# so Qt Creator always discovers runnable View and Stitch targets after a
# reconfigure, even if an old cache had disabled them.
set(PCV_BUILD_POINTCLOUDVIEW ON CACHE BOOL "Build the point cloud viewer" FORCE)
set(PCV_BUILD_POINTCLOUDSTITCH ON CACHE BOOL
    "Build the point cloud stitching application" FORCE)
option(PCV_BUILD_CLI "Build the PointCloudSuite CLI" OFF)
option(PCV_BUILD_TOOLS "Build diagnostic command-line tools" OFF)
option(PCV_BUILD_TESTS "Build all automated tests" OFF)
option(PCV_BUILD_EXPERIMENTAL_CAMERA
    "Build Hikvision camera acquisition and its UI/tests" ON)

# pointcloudview and pointcloudstitch are fixed desktop entrypoints, so their
# shared Qt/module stack is always required by the root project.
set(PCV_BUILD_SHARED_STACK ON)

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

# Keep the desktop applications directly connected to the root project. This
# makes both executable targets visible to Qt Creator after CMake reconfigure.
add_subdirectory(apps/pointcloudview)
add_subdirectory(apps/pointcloudstitch)
if(PCV_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
if(PCV_BUILD_CLI)
    add_subdirectory(apps/pointcloudcli)
endif()
if(PCV_BUILD_TESTS)
    add_subdirectory(tests)
endif()
