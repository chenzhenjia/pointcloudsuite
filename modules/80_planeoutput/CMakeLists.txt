add_library(pcv_m80_planeoutput STATIC
    src/plane_output.cpp
    include/pcv/output/plane_output.h
)
target_include_directories(pcv_m80_planeoutput PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m80_planeoutput PUBLIC pcv_infrastructure pcv_core Qt::Core Qt::Gui)
