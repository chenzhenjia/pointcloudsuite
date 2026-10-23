# OCCT and its vcpkg dependencies live in one configuration-specific bin.
# Copy only from that package directory, never from FreeCAD or a user's PATH.
function(pcv_deploy_step_runtime target)
    if(NOT WIN32 OR NOT PCV_BUILD_STEP_IMPORT)
        return()
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        get_target_property(cad_kernel TKernel IMPORTED_LOCATION_DEBUG)
    else()
        get_target_property(cad_kernel TKernel IMPORTED_LOCATION_RELEASE)
    endif()
    if(NOT cad_kernel)
        message(FATAL_ERROR "OCCT configuration has no matching shared TKernel runtime")
    endif()
    get_filename_component(cad_bin "${cad_kernel}" DIRECTORY)
    file(GLOB cad_dlls "${cad_bin}/*.dll")
    if(NOT cad_dlls)
        message(FATAL_ERROR "No OCCT shared runtime DLLs found in ${cad_bin}")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${cad_dlls} "$<TARGET_FILE_DIR:${target}>"
        VERBATIM)
    # vcpkg packages contain the original LGPL + OCCT exception in copyright.
    get_filename_component(cad_package "${OpenCASCADE_DIR}/../.." ABSOLUTE)
    if(EXISTS "${cad_package}/share/opencascade/copyright")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses/occt"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${cad_package}/share/opencascade/copyright"
                "$<TARGET_FILE_DIR:${target}>/licenses/occt/copyright.txt"
            VERBATIM)
        foreach(cad_dependency brotli bzip2 freetype libpng zlib)
            if(EXISTS "${cad_package}/share/${cad_dependency}/copyright")
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses/${cad_dependency}"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${cad_package}/share/${cad_dependency}/copyright"
                        "$<TARGET_FILE_DIR:${target}>/licenses/${cad_dependency}/copyright.txt"
                    VERBATIM)
            endif()
        endforeach()
    else()
        message(FATAL_ERROR "OCCT license file not found; use the documented vcpkg package")
    endif()
endfunction()
