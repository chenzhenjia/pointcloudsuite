add_library(pcv_m60_planefitting STATIC
    src/plane_fitting.cpp
    include/pcv/planefitting/plane_fitting.h
)
target_include_directories(pcv_m60_planefitting PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_m60_planefitting PUBLIC pcv_core Qt::Core Qt::Gui)
