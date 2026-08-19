add_library(pcv_io STATIC
    cloud_cache.cpp
    ${PROJECT_SOURCE_DIR}/include/pcv/io/cloud_cache.h
    ply_reader.cpp
    ${PROJECT_SOURCE_DIR}/include/pcv/io/ply_reader.h
)
target_include_directories(pcv_io PUBLIC
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_io PUBLIC pcv_core pcv_infrastructure Qt::Core)
