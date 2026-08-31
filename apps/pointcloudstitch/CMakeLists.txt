qt_add_executable(pointcloudstitch WIN32
    main.cpp
    regressionrunner.cpp
    regressionrunner.h
    stitchingwindow.cpp
    stitchingwindow.h
    stitchingwindow.ui
    seamfusion.cpp
    seamfusion.h
    pointcloudprocessor.cpp
    pointcloudprocessor.h
)

# Do not rely solely on localized MSVC /showIncludes output for the shared PLY
# reader contract; stale caller objects can otherwise corrupt return storage.
set_property(SOURCE pointcloudprocessor.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/pcv/io/ply_reader.h")

target_link_libraries(pointcloudstitch PRIVATE
    Qt::Core Qt::Gui Qt::Widgets Qt::Concurrent
    pcv_core pcv_infrastructure pcv_filtering
    pcv_m10_pointcloudread
    pcv_m30_pointcloudstitch
    pcv_m40_pointcloudregistration
    pcv_m50_coordinateconversion
    pcv_m80_planeoutput)

# Qt Creator detects this as the sole runnable desktop target. Keep its working
# directory beside the executable so direct runs and deployed runs behave alike.
set_target_properties(pointcloudstitch PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:pointcloudstitch>"
)
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT pointcloudstitch)

if(WIN32)
    add_custom_command(TARGET pointcloudstitch POST_BUILD
        COMMAND Qt6::windeployqt
            --$<IF:$<CONFIG:Debug>,debug,release>
            --compiler-runtime --no-translations
            "$<TARGET_FILE:pointcloudstitch>"
        COMMENT "Deploying Qt runtime next to pointcloudstitch.exe"
        VERBATIM)
endif()
