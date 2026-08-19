qt_add_executable(pointcloudstitch WIN32
    main.cpp
    regressionrunner.cpp
    regressionrunner.h
    stitchingwindow.cpp
    stitchingwindow.h
    stitchingwindow.ui
    seamfusion.cpp
    seamfusion.h
    handeye_transform.cpp
    handeye_transform.h
    pointcloudprocessor.cpp
    pointcloudprocessor.h
)

target_link_libraries(pointcloudstitch PRIVATE
    Qt::Core Qt::Gui Qt::Widgets Qt::Concurrent pcv_core pcv_infrastructure pcv_io pcv_filtering)

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
