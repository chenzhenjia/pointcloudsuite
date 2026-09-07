add_library(pcv_m20_pointcloud_render STATIC
    src/pointcloud_canvas.cpp
    src/pointcloud_canvas_contract.cpp
    src/overlay_geometry.h
    include/pcv/render/pointcloud_canvas.h
    include/pcv/render/pointcloud_canvas_contract.h
)
target_include_directories(pcv_m20_pointcloud_render PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m20_pointcloud_render PUBLIC
    pcv_core Qt::Core Qt::Gui Qt::Widgets Qt::OpenGL Qt::OpenGLWidgets)

add_library(pcv_m20_pointcloudrender INTERFACE)
target_link_libraries(pcv_m20_pointcloudrender INTERFACE pcv_m20_pointcloud_render)
