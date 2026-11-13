add_library(pcv_m50_coordinate_conversion STATIC
    src/rigid_estimation_v1.cpp
    include/pcv/registration/rigid_estimation_v1.h
    src/handeye_transform.cpp
    include/pcv/registration/handeye_transform.h
)
target_include_directories(pcv_m50_coordinate_conversion PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m50_coordinate_conversion PUBLIC pcv_core Qt::Core Qt::Gui)
