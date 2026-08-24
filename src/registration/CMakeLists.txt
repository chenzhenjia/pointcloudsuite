add_library(pcv_registration STATIC
    handeye_transform.cpp
    ${PROJECT_SOURCE_DIR}/include/pcv/registration/handeye_transform.h
)
target_include_directories(pcv_registration PUBLIC
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_registration PUBLIC pcv_core Qt::Core Qt::Gui)
