add_library(pcv_m50_coordinateconversion STATIC
    src/handeye_transform.cpp
    include/pcv/registration/handeye_transform.h
)
target_include_directories(pcv_m50_coordinateconversion PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m50_coordinateconversion PUBLIC pcv_core Qt::Core Qt::Gui)
