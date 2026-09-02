# v0.3 module targets. 05_camerainput is explicitly experimental and is not
# part of the v0.3 release baseline.
if(PCV_BUILD_EXPERIMENTAL_CAMERA)
    add_subdirectory(05_camerainput)
endif()
add_subdirectory(10_pointcloudread)
add_subdirectory(20_pointcloudrender)
add_subdirectory(30_pointcloudstitch)
add_subdirectory(40_pointcloudregistration)
add_subdirectory(50_coordinateconversion)
add_subdirectory(60_planefitting)
add_subdirectory(70_roi_template)
add_subdirectory(80_planeoutput)
