qt_add_executable(pointcloudview
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    ui/mainwindow.ui
    registrationdialog.cpp
    registrationdialog.h
    pointcloudprocessor.cpp
    pointcloudprocessor.h
)
if(PCV_BUILD_EXPERIMENTAL_CAMERA)
    target_sources(pointcloudview PRIVATE
        live_pipeline.cpp
        live_pipeline.h
        cameradialog.cpp
        cameradialog.h
    )
endif()
set_property(TARGET pointcloudview PROPERTY AUTOUIC_SEARCH_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui")

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
        pcv_m10_pointcloudread
        pcv_m20_pointcloudrender
        pcv_filtering
        pcv_m60_planefitting
        pcv_m70_roi_template
        pcv_m30_pointcloudstitch
        pcv_m80_planeoutput
)
if(PCV_BUILD_EXPERIMENTAL_CAMERA)
    target_link_libraries(pointcloudview PRIVATE pcv_m05_camerainput)
    target_compile_definitions(pointcloudview PRIVATE PCV_BUILD_EXPERIMENTAL_CAMERA=1)
endif()

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
    if(PCV_BUILD_EXPERIMENTAL_CAMERA)
        # Deploy the Hikvision Mv3dLp runtime only for the experimental UI.
        set(PCV_MV3D_RUNTIME_DIR
            "C:/Program Files (x86)/Common Files/Mv3dLpSDK/Runtime/Win64_x64")
        if(EXISTS "${PCV_MV3D_RUNTIME_DIR}/Mv3dLp.dll")
            add_custom_command(TARGET pointcloudview POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${PCV_MV3D_RUNTIME_DIR}"
                    "$<TARGET_FILE_DIR:pointcloudview>"
                COMMENT "Deploying Mv3dLp SDK runtime next to pointcloudview.exe"
                VERBATIM
            )
        endif()
    endif()
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
