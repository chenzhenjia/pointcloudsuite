add_library(pcv_m20_pointcloudrender STATIC
    src/pointcloud_canvas_contract.cpp
    include/pcv/render/pointcloud_canvas.h
    include/pcv/render/pointcloud_canvas_contract.h
)
target_include_directories(pcv_m20_pointcloudrender PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m20_pointcloudrender PUBLIC
    pcv_core Qt::Core Qt::Gui Qt::Widgets Qt::OpenGL Qt::OpenGLWidgets)
