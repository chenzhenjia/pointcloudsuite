add_library(pcv_registration STATIC
    handeye_transform.cpp
    multiframe_registration.cpp
    seam_fusion.cpp
    ${PROJECT_SOURCE_DIR}/include/pcv/registration/handeye_transform.h
    ${PROJECT_SOURCE_DIR}/include/pcv/registration/multiframe_registration.h
    ${PROJECT_SOURCE_DIR}/include/pcv/registration/seam_fusion.h
)
target_include_directories(pcv_registration PUBLIC
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_registration PUBLIC pcv_core pcv_filtering Qt::Core Qt::Gui)
