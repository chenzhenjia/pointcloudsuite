add_library(pcv_m10_pointcloudread STATIC
    src/cloud_cache.cpp
    src/ply_reader.cpp
    src/ply_writer.cpp
    include/pcv/io/cloud_cache.h
    include/pcv/io/ply_reader.h
    include/pcv/io/ply_writer.h
)
target_include_directories(pcv_m10_pointcloudread PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m10_pointcloudread PUBLIC pcv_core pcv_infrastructure Qt::Core)
