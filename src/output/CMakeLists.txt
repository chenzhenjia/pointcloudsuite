add_library(pcv_output STATIC
    plane_output.cpp
    ${PROJECT_SOURCE_DIR}/include/pcv/output/plane_output.h
)
target_include_directories(pcv_output PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_output PUBLIC pcv_infrastructure pcv_core Qt::Core Qt::Gui)
