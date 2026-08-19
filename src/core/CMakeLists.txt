add_library(pcv_core INTERFACE)
target_include_directories(pcv_core INTERFACE
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_core INTERFACE Qt::Core)
