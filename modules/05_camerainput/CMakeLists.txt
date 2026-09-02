# 05_camerainput: Hikvision Mv3dLp point-cloud frame acquisition.
#
# The pure conversion / frame-slot / mock-source parts build without the
# vendor SDK so tests and the pipeline can run on machines without a camera.
# The live camera wrapper is compiled only when the SDK is found.

# --- Vendor SDK discovery -------------------------------------------------
set(PCV_HIKSDK_ROOT "" CACHE PATH
    "Hikvision Mv3dLp SDK root (contains Includes/ and Libraries/win64)")
if(NOT PCV_HIKSDK_ROOT)
    if(EXISTS "C:/Program Files (x86)/3DMVS/Development")
        set(PCV_HIKSDK_ROOT "C:/Program Files (x86)/3DMVS/Development")
    endif()
endif()

set(PCV_CAMERA_HAVE_HIKSDK OFF)
if(PCV_HIKSDK_ROOT)
    if(EXISTS "${PCV_HIKSDK_ROOT}/Includes/Mv3dLpApi.h"
       AND EXISTS "${PCV_HIKSDK_ROOT}/Libraries/win64/Mv3dLp.lib")
        set(PCV_CAMERA_HAVE_HIKSDK ON)
        message(STATUS "05_camerainput: Hikvision Mv3dLp SDK found at ${PCV_HIKSDK_ROOT}")
    else()
        message(WARNING
            "05_camerainput: PCV_HIKSDK_ROOT=${PCV_HIKSDK_ROOT} is set but the SDK "
            "headers/library were not found; building without live camera support.")
    endif()
endif()

add_library(pcv_m05_camerainput STATIC
    src/frame_converter.cpp
    src/latest_frame_slot.cpp
    src/mock_frame_source.cpp
    include/pcv/camera/frame_data.h
    include/pcv/camera/frame_converter.h
    include/pcv/camera/frame_source.h
    include/pcv/camera/mock_frame_source.h
)
target_include_directories(pcv_m05_camerainput PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m05_camerainput PUBLIC
    pcv_core Qt::Core)

if(PCV_CAMERA_HAVE_HIKSDK)
    target_sources(pcv_m05_camerainput PRIVATE
        src/mv3d_lp_camera.cpp
        include/pcv/camera/mv3d_lp_camera.h
    )
    target_include_directories(pcv_m05_camerainput PRIVATE
        "${PCV_HIKSDK_ROOT}/Includes")
    target_link_libraries(pcv_m05_camerainput PRIVATE
        "${PCV_HIKSDK_ROOT}/Libraries/win64/Mv3dLp.lib")
    target_compile_definitions(pcv_m05_camerainput PUBLIC PCV_CAMERA_HAVE_HIKSDK=1)
endif()
