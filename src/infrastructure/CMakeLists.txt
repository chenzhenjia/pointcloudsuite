add_library(pcv_infrastructure STATIC
    application_config.cpp
    runtime_paths.cpp
    ${PROJECT_SOURCE_DIR}/include/pcv/infrastructure/application_config.h
    ${PROJECT_SOURCE_DIR}/include/pcv/infrastructure/runtime_paths.h
)
target_include_directories(pcv_infrastructure PUBLIC
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_infrastructure PUBLIC Qt::Core)
