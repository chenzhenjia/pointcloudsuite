if(NOT DEFINED PCV_CLI OR NOT DEFINED PCV_MODE)
    message(FATAL_ERROR "PCV_CLI and PCV_MODE are required")
endif()

set(PCV_ROOT "${CMAKE_CURRENT_BINARY_DIR}/cli-job-${PCV_MODE}-中文")
file(REMOVE_RECURSE "${PCV_ROOT}")
file(MAKE_DIRECTORY "${PCV_ROOT}")

file(WRITE "${PCV_ROOT}/scan.ply" [=[ply
format ascii 1.0
element vertex 9
property float x
property float y
property float z
end_header
-10 0 1
0 0 1
10 0 1
-10 5 1
0 5 1
10 5 1
-10 10 1
0 10 1
10 10 1
]=])
file(WRITE "${PCV_ROOT}/calibration.xml" [=[<ArithConfig><RTmatDepth2robot><RotMat r00="1" r01="0" r02="0" r10="0" r11="1" r12="0" r20="0" r21="0" r22="1"/><TVec t0="0" t1="0" t2="0"/></RTmatDepth2robot></ArithConfig>]=])
file(WRITE "${PCV_ROOT}/temp_scanning_info.json" [=[{"schema_version":"sr2026-temp-scanning-info-mvp-2","kind":"single_frame_scanning_info","created_at":"2026-09-09T12:00:00.000+08:00","scan":{"scan_id":"scan-cli-test","point_cloud_file":"scan.ply","point_cloud_layout":"LineProfileXz","coordinate_frame":"camera","robot_pose_start":[0,0,0,0,0,0],"robot_pose_end":[0,10,0,0,0,0]},"calibration":{"calibration_file":"calibration.xml","source_frame":"camera","target_frame":"robot_base"}}]=])

if(PCV_MODE STREQUAL "traversal")
    set(PCV_JOB [=[{"schema_version":"pointcloudcli-temp-workpiece-job-v1","kind":"temp_workpiece_job","job_id":"job-traversal","scanning_info_file":"../scan.ply","calibration_override_file":"","output_directory":"output/job-traversal","plane":{"seed_indices":[0,2,4],"verification_indices":[1,3,5],"plane_indices":[],"angle_tolerance_deg":1.0,"distance_tolerance_mm":0.4},"roi":{"mode":"rectangle","roi_indices":[],"width_mm":20.0,"height_mm":20.0}}]=])
else()
    if(PCV_MODE STREQUAL "indices")
        set(PCV_ROI [=["mode":"indices","roi_indices":[0,1,2],"width_mm":20.0,"height_mm":20.0]=])
        set(PCV_EXPECTED_ROI "\"roi_points\":3")
        set(PCV_JOB_ID "job-indices")
        set(PCV_OUTPUT "output/job-indices")
    else()
        set(PCV_ROI [=["mode":"rectangle","roi_indices":[],"width_mm":20.0,"height_mm":20.0]=])
        set(PCV_EXPECTED_ROI "\"roi_points\":9")
        set(PCV_JOB_ID "job-rectangle")
        set(PCV_OUTPUT "output/job-rectangle")
    endif()
    set(PCV_JOB "{\"schema_version\":\"pointcloudcli-temp-workpiece-job-v1\",\"kind\":\"temp_workpiece_job\",\"job_id\":\"${PCV_JOB_ID}\",\"scanning_info_file\":\"temp_scanning_info.json\",\"calibration_override_file\":\"\",\"output_directory\":\"${PCV_OUTPUT}\",\"allow_overwrite\":false,\"plane\":{\"seed_indices\":[0,2,4],\"verification_indices\":[1,3,5],\"plane_indices\":[],\"angle_tolerance_deg\":1.0,\"distance_tolerance_mm\":0.4},\"roi\":{${PCV_ROI}}}")
endif()
file(WRITE "${PCV_ROOT}/job.json" "${PCV_JOB}")

if(PCV_PORTABLE)
    get_filename_component(PCV_PORTABLE_DIR "${PCV_CLI}" DIRECTORY)
    set(ENV{PATH} "${PCV_PORTABLE_DIR}")
endif()

execute_process(
    COMMAND "${PCV_CLI}" temp-workpiece run "${PCV_ROOT}/job.json" --data-dir "${PCV_ROOT}/data"
    RESULT_VARIABLE PCV_EXIT
    OUTPUT_VARIABLE PCV_STDOUT
    ERROR_VARIABLE PCV_STDERR
    TIMEOUT 30)

if(PCV_MODE STREQUAL "traversal")
    if(NOT PCV_EXIT EQUAL 2)
        message(FATAL_ERROR "Traversal must return 2, got ${PCV_EXIT}: ${PCV_STDERR}")
    endif()
    if(NOT PCV_STDERR MATCHES "PCV_CONTRACT_001")
        message(FATAL_ERROR "Traversal error contract missing: ${PCV_STDERR}")
    endif()
    return()
endif()

if(NOT PCV_EXIT EQUAL 0)
    message(FATAL_ERROR "CLI job failed with ${PCV_EXIT}: stdout=${PCV_STDOUT} stderr=${PCV_STDERR}")
endif()
if(NOT PCV_STDOUT MATCHES "\"ok\":true" OR NOT PCV_STDOUT MATCHES "${PCV_EXPECTED_ROI}")
    message(FATAL_ERROR "Unexpected success summary: ${PCV_STDOUT}")
endif()
if(NOT PCV_STDOUT MATCHES "\"data_dir\":\".*/data\"")
    message(FATAL_ERROR "CLI did not report requested data directory: ${PCV_STDOUT}")
endif()
file(GLOB PCV_CACHE_FILES "${PCV_ROOT}/data/cache/*")
if(NOT PCV_CACHE_FILES)
    message(FATAL_ERROR "CLI did not create a cache file under requested data directory")
endif()
set(PCV_OUTPUT_ROOT "${PCV_ROOT}/${PCV_OUTPUT}")
foreach(PCV_FILE IN ITEMS baseline_robot_base.ply roi_template_robot_base.ply plane_mask.png temp_workpiece_info.json)
    if(NOT EXISTS "${PCV_OUTPUT_ROOT}/${PCV_FILE}")
        message(FATAL_ERROR "Missing output ${PCV_OUTPUT_ROOT}/${PCV_FILE}")
    endif()
endforeach()
file(GLOB PCV_STAGING "${PCV_OUTPUT_ROOT}/.temp-workpiece-staging-*")
if(PCV_STAGING)
    message(FATAL_ERROR "Staging directory was not cleaned: ${PCV_STAGING}")
endif()

if(PCV_MODE STREQUAL "overwrite")
    execute_process(
        COMMAND "${PCV_CLI}" temp-workpiece run "${PCV_ROOT}/job.json" --data-dir "${PCV_ROOT}/data"
        RESULT_VARIABLE PCV_REFUSED_EXIT
        OUTPUT_VARIABLE PCV_REFUSED_STDOUT
        ERROR_VARIABLE PCV_REFUSED_STDERR
        TIMEOUT 30)
    if(NOT PCV_REFUSED_EXIT EQUAL 40 OR NOT PCV_REFUSED_STDERR MATCHES "PCV_OUTPUT_002")
        message(FATAL_ERROR "Overwrite protection failed: exit=${PCV_REFUSED_EXIT} stderr=${PCV_REFUSED_STDERR}")
    endif()
    execute_process(
        COMMAND "${PCV_CLI}" temp-workpiece run "${PCV_ROOT}/job.json" --allow-overwrite --data-dir "${PCV_ROOT}/data"
        RESULT_VARIABLE PCV_OVERWRITE_EXIT
        OUTPUT_VARIABLE PCV_OVERWRITE_STDOUT
        ERROR_VARIABLE PCV_OVERWRITE_STDERR
        TIMEOUT 30)
    if(NOT PCV_OVERWRITE_EXIT EQUAL 0 OR NOT PCV_OVERWRITE_STDOUT MATCHES "\"ok\":true")
        message(FATAL_ERROR "Explicit overwrite failed: exit=${PCV_OVERWRITE_EXIT} stdout=${PCV_OVERWRITE_STDOUT} stderr=${PCV_OVERWRITE_STDERR}")
    endif()
endif()

if(PCV_MODE STREQUAL "rectangle")
    set(PCV_ENV_DATA "${PCV_ROOT}/env-data")
    set(PCV_ENV_JOB "${PCV_ROOT}/env-job.json")
    file(WRITE "${PCV_ENV_JOB}" [=[{"schema_version":"pointcloudcli-temp-workpiece-job-v1","kind":"temp_workpiece_job","job_id":"job-env","scanning_info_file":"temp_scanning_info.json","calibration_override_file":"","output_directory":"output/job-env","allow_overwrite":false,"plane":{"seed_indices":[0,2,4],"verification_indices":[1,3,5],"plane_indices":[],"angle_tolerance_deg":1.0,"distance_tolerance_mm":0.4},"roi":{"mode":"rectangle","roi_indices":[],"width_mm":20.0,"height_mm":20.0}}]=])
    set(PCV_PREVIOUS_DATA_DIR "$ENV{PCV_DATA_DIR}")
    set(ENV{PCV_DATA_DIR} "${PCV_ENV_DATA}")
    execute_process(
        COMMAND "${PCV_CLI}" temp-workpiece run "${PCV_ENV_JOB}"
        RESULT_VARIABLE PCV_ENV_EXIT
        OUTPUT_VARIABLE PCV_ENV_STDOUT
        ERROR_VARIABLE PCV_ENV_STDERR
        TIMEOUT 30)
    set(ENV{PCV_DATA_DIR} "${PCV_PREVIOUS_DATA_DIR}")
    if(NOT PCV_ENV_EXIT EQUAL 0 OR NOT PCV_ENV_STDOUT MATCHES "\"data_dir\":\".*/env-data\"")
        message(FATAL_ERROR "PCV_DATA_DIR was not selected: exit=${PCV_ENV_EXIT} stdout=${PCV_ENV_STDOUT} stderr=${PCV_ENV_STDERR}")
    endif()
    file(GLOB PCV_ENV_CACHE_FILES "${PCV_ENV_DATA}/cache/*")
    if(NOT PCV_ENV_CACHE_FILES)
        message(FATAL_ERROR "PCV_DATA_DIR did not receive the CLI cache")
    endif()
endif()
