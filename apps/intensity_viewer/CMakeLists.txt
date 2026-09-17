qt_add_executable(intensity_viewer
    WIN32 MACOSX_BUNDLE
    main.cpp
    intensityviewerwindow.cpp
    intensityviewerwindow.h
    ui/intensityviewerwindow.ui
)

set_property(TARGET intensity_viewer PROPERTY AUTOUIC_SEARCH_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui")

target_link_libraries(intensity_viewer
    PRIVATE
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Concurrent
        pcv_m10_pointcloud_io
)

if(PCV_BUILD_EXPERIMENTAL_CAMERA)
    target_compile_definitions(intensity_viewer PRIVATE PCV_BUILD_EXPERIMENTAL_CAMERA=1)
endif()

if(WIN32)
    add_custom_command(TARGET intensity_viewer POST_BUILD
        COMMAND Qt6::windeployqt
            --$<IF:$<CONFIG:Debug>,debug,release>
            --compiler-runtime
            --no-translations
            "$<TARGET_FILE:intensity_viewer>"
        COMMENT "Deploying Qt runtime next to intensity_viewer.exe"
        VERBATIM
    )
    if(PCV_BUILD_EXPERIMENTAL_CAMERA)
        set(PCV_MV3D_RUNTIME_DIR
            "C:/Program Files (x86)/Common Files/Mv3dLpSDK/Runtime/Win64_x64")
        if(EXISTS "${PCV_MV3D_RUNTIME_DIR}/Mv3dLp.dll")
            add_custom_command(TARGET intensity_viewer POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${PCV_MV3D_RUNTIME_DIR}"
                    "$<TARGET_FILE_DIR:intensity_viewer>"
                COMMENT "Deploying Mv3dLp SDK runtime next to intensity_viewer.exe"
                VERBATIM
            )
        endif()
    endif()
endif()

install(TARGETS intensity_viewer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
