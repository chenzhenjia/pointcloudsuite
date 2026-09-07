add_library(pcv_m80_plane_output STATIC
    src/plane_output.cpp
    include/pcv/output/plane_output.h
)
target_include_directories(pcv_m80_plane_output PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m80_plane_output PUBLIC pcv_infrastructure pcv_core pcv_m10_pointcloud_read Qt::Core Qt::Gui)

add_library(pcv_m80_planeoutput INTERFACE)
target_link_libraries(pcv_m80_planeoutput INTERFACE pcv_m80_plane_output)
