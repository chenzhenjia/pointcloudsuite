qt_add_executable(pointcloudview
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    pointcloudprocessor.cpp
    pointcloudprocessor.h
)

# Localized MSVC /showIncludes output can leave an empty dependency file with
# this Qt Creator Makefile kit. Keep this ABI-critical dependency explicit.
set_property(SOURCE pointcloudprocessor.cpp APPEND PROPERTY OBJECT_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/pcv/io/ply_reader.h")

target_link_libraries(pointcloudview
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Concurrent
        Qt::OpenGL
        Qt::OpenGLWidgets
        pcv_core
        pcv_infrastructure
        pcv_io
        pcv_filtering
        pcv_output
        pcv_interface
)

if (WIN32)
    add_custom_command(TARGET pointcloudview POST_BUILD
        COMMAND Qt6::windeployqt
            --$<IF:$<CONFIG:Debug>,debug,release>
            --compiler-runtime
            --no-translations
            "$<TARGET_FILE:pointcloudview>"
        COMMENT "Deploying Qt runtime next to pointcloudview.exe"
        VERBATIM
    )
endif()

install(TARGETS pointcloudview
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET pointcloudview
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
