add_library(pcv_interface STATIC
    temp_workpiece_interface.cpp
    ${PROJECT_SOURCE_DIR}/include/pcv/interface/temp_workpiece_interface.h
)
target_include_directories(pcv_interface PUBLIC
    ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(pcv_interface PUBLIC
    pcv_core pcv_infrastructure pcv_io pcv_registration Qt::Core Qt::Gui)
