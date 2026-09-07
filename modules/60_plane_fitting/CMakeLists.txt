add_library(pcv_m60_plane_fitting STATIC
    src/plane_fitting.cpp
    include/pcv/planefitting/plane_fitting.h
)
target_include_directories(pcv_m60_plane_fitting PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m60_plane_fitting PUBLIC pcv_core Qt::Core Qt::Gui)

add_library(pcv_m60_planefitting INTERFACE)
target_link_libraries(pcv_m60_planefitting INTERFACE pcv_m60_plane_fitting)
