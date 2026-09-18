set(PCV_HIKSDK_ROOT "" CACHE PATH
    "Hikvision Mv3dLp SDK root (contains Includes/ and Libraries/win64)")
if(NOT PCV_HIKSDK_ROOT)
    if(EXISTS "C:/Program Files (x86)/3DMVS/Development")
        set(PCV_HIKSDK_ROOT "C:/Program Files (x86)/3DMVS/Development")
    endif()
endif()

set(PCV_CAMERA_HAVE_HIKSDK OFF)
if(PCV_BUILD_EXPERIMENTAL_CAMERA AND PCV_HIKSDK_ROOT)
    if(EXISTS "${PCV_HIKSDK_ROOT}/Includes/Mv3dLpApi.h"
       AND EXISTS "${PCV_HIKSDK_ROOT}/Libraries/win64/Mv3dLp.lib")
        set(PCV_CAMERA_HAVE_HIKSDK ON)
        message(STATUS "10_pointcloud_read: Hikvision Mv3dLp SDK found at ${PCV_HIKSDK_ROOT}")
    else()
        message(WARNING
            "10_pointcloud_read: PCV_HIKSDK_ROOT=${PCV_HIKSDK_ROOT} is set but the SDK "
            "headers/library were not found; building without live camera support.")
    endif()
endif()

add_library(pcv_m10_pointcloud_io STATIC
    src/cloud_cache.cpp
    src/artifact_transaction.cpp
    src/frame_converter.cpp
    src/intensity_image.cpp
    src/latest_frame_slot.cpp
    src/mock_frame_source.cpp
    src/ply_reader.cpp
    src/ply_writer.cpp
    src/live_frame_policy.h
    src/scan_frame_assembler.h
    src/trigger_mode_policy.h
    src/downsample.cpp
    src/statistical_filter.cpp
    src/plane_output.cpp
    include/pcv/camera/frame_data.h
    include/pcv/camera/frame_converter.h
    include/pcv/camera/intensity_image.h
    include/pcv/camera/frame_source.h
    include/pcv/camera/mock_frame_source.h
    include/pcv/camera/mv3d_lp_camera.h
    include/pcv/io/cloud_cache.h
    include/pcv/io/artifact_transaction.h
    include/pcv/io/ply_reader.h
    include/pcv/io/ply_writer.h
    include/pcv/io/plane_output.h
    include/pcv/processing/downsample.h
    include/pcv/processing/statistical_filter.h
)
target_include_directories(pcv_m10_pointcloud_io PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m10_pointcloud_io PUBLIC pcv_core pcv_infrastructure Qt::Core Qt::Gui)

if(PCV_CAMERA_HAVE_HIKSDK)
    target_sources(pcv_m10_pointcloud_io PRIVATE src/mv3d_lp_camera.cpp)
    target_include_directories(pcv_m10_pointcloud_io PRIVATE
        "${PCV_HIKSDK_ROOT}/Includes")
    target_link_libraries(pcv_m10_pointcloud_io PRIVATE
        "${PCV_HIKSDK_ROOT}/Libraries/win64/Mv3dLp.lib")
    target_compile_definitions(pcv_m10_pointcloud_io PUBLIC PCV_CAMERA_HAVE_HIKSDK=1)
endif()

add_library(pcv_m10_pointcloud_read INTERFACE)
target_link_libraries(pcv_m10_pointcloud_read INTERFACE pcv_m10_pointcloud_io)

# Migration aliases. New code must link pcv_m10_pointcloud_read; these names
# remain available for downstream callers during the compatibility period.
add_library(pcv_m10_pointcloudread INTERFACE)
target_link_libraries(pcv_m10_pointcloudread INTERFACE pcv_m10_pointcloud_io)
add_library(pcv_m05_camerainput INTERFACE)
target_link_libraries(pcv_m05_camerainput INTERFACE pcv_m10_pointcloud_io)
