if(NOT DEFINED PCV_CLI OR PCV_CLI STREQUAL "")
    message(FATAL_ERROR "PCV_CLI is required")
endif()
if(NOT DEFINED PCV_MODE)
    message(FATAL_ERROR "PCV_MODE is required")
endif()

set(PCV_EXPECTED_EXIT 0)
if(PCV_MODE STREQUAL "help")
    set(PCV_ARGUMENTS --help)
    set(PCV_EXPECTED_TEXT
        "Usage: pointcloudcli [--help] [--version]"
        "Options:"
        "--help"
        "--version")
elseif(PCV_MODE STREQUAL "version")
    set(PCV_ARGUMENTS --version)
    set(PCV_EXPECTED_TEXT "pointcloudcli ${PCV_PROJECT_VERSION}")
elseif(PCV_MODE STREQUAL "empty")
    set(PCV_EXPECTED_EXIT 2)
    set(PCV_EXPECTED_TEXT
        "error: missing command-line option"
        "Usage: pointcloudcli [--help] [--version]")
elseif(PCV_MODE STREQUAL "unknown")
    set(PCV_ARGUMENTS --unknown)
    set(PCV_EXPECTED_EXIT 2)
    set(PCV_EXPECTED_TEXT
        "error: unknown option: --unknown"
        "Usage: pointcloudcli [--help] [--version]")
else()
    message(FATAL_ERROR "Unsupported PCV_MODE: ${PCV_MODE}")
endif()

if(PCV_PORTABLE)
    get_filename_component(PCV_PORTABLE_DIR "${PCV_CLI}" DIRECTORY)
    set(ENV{PATH} "${PCV_PORTABLE_DIR}")
endif()

execute_process(
    COMMAND "${PCV_CLI}" ${PCV_ARGUMENTS}
    RESULT_VARIABLE PCV_ACTUAL_EXIT
    OUTPUT_VARIABLE PCV_STDOUT
    ERROR_VARIABLE PCV_STDERR
)

if(NOT PCV_ACTUAL_EXIT EQUAL PCV_EXPECTED_EXIT)
    message(FATAL_ERROR
        "Unexpected exit code for ${PCV_MODE}: expected ${PCV_EXPECTED_EXIT}, "
        "got ${PCV_ACTUAL_EXIT}. stdout=[${PCV_STDOUT}] stderr=[${PCV_STDERR}]")
endif()

set(PCV_OUTPUT "${PCV_STDOUT}${PCV_STDERR}")
foreach(PCV_TEXT IN LISTS PCV_EXPECTED_TEXT)
    string(FIND "${PCV_OUTPUT}" "${PCV_TEXT}" PCV_TEXT_OFFSET)
    if(PCV_TEXT_OFFSET EQUAL -1)
        message(FATAL_ERROR
            "Missing expected text for ${PCV_MODE}: [${PCV_TEXT}]. "
            "stdout=[${PCV_STDOUT}] stderr=[${PCV_STDERR}]")
    endif()
endforeach()
