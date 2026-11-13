if(NOT DEFINED PCV_CLI OR NOT DEFINED PCV_MODE)
    message(FATAL_ERROR "PCV_CLI and PCV_MODE are required")
endif()

set(PCV_ROOT "${CMAKE_CURRENT_BINARY_DIR}/cli-ply-${PCV_MODE}")
file(REMOVE_RECURSE "${PCV_ROOT}")
file(MAKE_DIRECTORY "${PCV_ROOT}")

if(PCV_MODE STREQUAL "inspect_ascii")
    set(PCV_REJECT_SUBCOMMAND inspect)
    file(WRITE "${PCV_ROOT}/input.ply" [=[ply
format ascii 1.0
element vertex 2
property float x
property float y
property float z
end_header
0 1 2
3 4 5
]=])
    set(PCV_ARGS inspect ply "${PCV_ROOT}/input.ply" --json)
    set(PCV_EXPECTED_FORMAT "\"format\":\"ascii\"")
elseif(PCV_MODE STREQUAL "inspect_binary_le")
    set(PCV_REJECT_SUBCOMMAND inspect)
    file(WRITE "${PCV_ROOT}/input.ply" [=[ply
format binary_little_endian 1.0
element vertex 2
property float x
property float y
property float z
end_header
]=])
    set(PCV_ARGS inspect ply "${PCV_ROOT}/input.ply" --json)
    set(PCV_EXPECTED_FORMAT "\"format\":\"binary_little_endian\"")
elseif(PCV_MODE STREQUAL "inspect_binary_be")
    set(PCV_REJECT_SUBCOMMAND inspect)
    file(WRITE "${PCV_ROOT}/input.ply" [=[ply
format binary_big_endian 1.0
element vertex 2
property float x
property float y
property float z
end_header
]=])
    set(PCV_ARGS inspect ply "${PCV_ROOT}/input.ply" --json)
    set(PCV_EXPECTED_FORMAT "\"format\":\"binary_big_endian\"")
elseif(PCV_MODE STREQUAL "inspect_missing")
    set(PCV_REJECT_SUBCOMMAND inspect)
    set(PCV_ARGS inspect ply "${PCV_ROOT}/missing.ply" --json)
    set(PCV_EXPECTED_EXIT 10)
    set(PCV_EXPECTED_ERROR "PCV_INPUT_001")
elseif(PCV_MODE STREQUAL "read_invalid")
    set(PCV_REJECT_SUBCOMMAND read)
    file(WRITE "${PCV_ROOT}/invalid.ply" "not a ply\n")
    set(PCV_ARGS read ply "${PCV_ROOT}/invalid.ply" --json)
    set(PCV_EXPECTED_EXIT 10)
    set(PCV_EXPECTED_ERROR "PCV_INPUT_002")
elseif(PCV_MODE STREQUAL "read_summary" OR PCV_MODE STREQUAL "read_bounds")
    set(PCV_REJECT_SUBCOMMAND read)
    file(WRITE "${PCV_ROOT}/input.ply" [=[ply
format ascii 1.0
element vertex 3
property float x
property float y
property float z
property float intensity
end_header
-1 2 3 10
4 -5 6 20
7 8 9 30
]=])
    set(PCV_ARGS read ply "${PCV_ROOT}/input.ply" --json)
    set(PCV_EXPECTED_FORMAT "\"format\":\"ascii\"")
else()
    message(FATAL_ERROR "Unsupported PCV_MODE: ${PCV_MODE}")
endif()

if(NOT DEFINED PCV_EXPECTED_EXIT)
    set(PCV_EXPECTED_EXIT 0)
endif()
if(PCV_PORTABLE)
    get_filename_component(PCV_PORTABLE_DIR "${PCV_CLI}" DIRECTORY)
    set(ENV{PATH} "${PCV_PORTABLE_DIR}")
endif()
execute_process(
    COMMAND "${PCV_CLI}" ${PCV_ARGS}
    RESULT_VARIABLE PCV_EXIT
    OUTPUT_VARIABLE PCV_STDOUT
    ERROR_VARIABLE PCV_STDERR
    TIMEOUT 30)

execute_process(
    COMMAND "${PCV_CLI}" ${PCV_REJECT_SUBCOMMAND} ply "${PCV_ROOT}/input.ply" --data-dir "${PCV_ROOT}/data"
    RESULT_VARIABLE PCV_DATA_DIR_EXIT
    OUTPUT_VARIABLE PCV_DATA_DIR_STDOUT
    ERROR_VARIABLE PCV_DATA_DIR_STDERR
    TIMEOUT 30)
if(NOT PCV_DATA_DIR_EXIT EQUAL 2 OR NOT PCV_DATA_DIR_STDERR MATCHES "PCV_CONTRACT_001")
    message(FATAL_ERROR "PLY command must reject --data-dir: exit=${PCV_DATA_DIR_EXIT} stdout=${PCV_DATA_DIR_STDOUT} stderr=${PCV_DATA_DIR_STDERR}")
endif()

if(NOT PCV_EXIT EQUAL PCV_EXPECTED_EXIT)
    message(FATAL_ERROR "Unexpected exit ${PCV_EXIT}: stdout=${PCV_STDOUT} stderr=${PCV_STDERR}")
endif()
if(DEFINED PCV_EXPECTED_ERROR)
    if(NOT PCV_STDERR MATCHES "${PCV_EXPECTED_ERROR}")
        message(FATAL_ERROR "Missing error ${PCV_EXPECTED_ERROR}: ${PCV_STDERR}")
    endif()
    return()
endif()
if(NOT PCV_STDOUT MATCHES "\"ok\":true" OR NOT PCV_STDOUT MATCHES "${PCV_EXPECTED_FORMAT}")
    message(FATAL_ERROR "Unexpected output: ${PCV_STDOUT}")
endif()
if(PCV_MODE STREQUAL "inspect_ascii" OR PCV_MODE STREQUAL "inspect_binary_le" OR PCV_MODE STREQUAL "inspect_binary_be")
    if(NOT PCV_STDOUT MATCHES "\"declared_points\":2" OR NOT PCV_STDOUT MATCHES "\"canonical_format\":\"binary_little_endian\"")
        message(FATAL_ERROR "Unexpected inspect output: ${PCV_STDOUT}")
    endif()
elseif(PCV_MODE STREQUAL "read_summary" OR PCV_MODE STREQUAL "read_bounds")
    if(NOT PCV_STDOUT MATCHES "\"loaded_points\":3" OR NOT PCV_STDOUT MATCHES "\"rejected_invalid\":0" OR NOT PCV_STDOUT MATCHES "\"has_intensity\":true")
        message(FATAL_ERROR "Unexpected read summary: ${PCV_STDOUT}")
    endif()
    if(NOT PCV_STDOUT MATCHES "\"min\":[[]-1,[-+]?[0-9.]+,3[]]" OR NOT PCV_STDOUT MATCHES "\"max\":[[]7,8,9[]]")
        message(FATAL_ERROR "Bounds missing from read summary: ${PCV_STDOUT}")
    endif()
endif()
