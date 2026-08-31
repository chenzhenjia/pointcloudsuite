add_library(pcv_m40_pointcloudregistration STATIC
    src/multiframe_registration.cpp
    src/seam_fusion.cpp
    include/pcv/registration/multiframe_registration.h
    include/pcv/registration/seam_fusion.h
)
target_include_directories(pcv_m40_pointcloudregistration PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m40_pointcloudregistration PUBLIC pcv_core pcv_filtering Qt::Core Qt::Gui)
