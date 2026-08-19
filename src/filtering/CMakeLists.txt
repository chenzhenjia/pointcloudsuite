add_library(pcv_filtering STATIC
    downsample.cpp
    ${PROJECT_SOURCE_DIR}/include/pcv/filtering/downsample.h
    statistical_filter.cpp
    ${PROJECT_SOURCE_DIR}/include/pcv/filtering/statistical_filter.h
)
target_include_directories(pcv_filtering PUBLIC
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_filtering PUBLIC pcv_core Qt::Core)
