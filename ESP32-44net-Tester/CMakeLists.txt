cmake_minimum_required(VERSION 3.16)

# Firmware version comes from git and is reported by the device as version=.
# Falls back gracefully when git metadata is unavailable (e.g. tarball builds).
execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE TESTER_GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT TESTER_GIT_VERSION)
    set(TESTER_GIT_VERSION "unknown")
endif()
add_compile_definitions(TESTER_VERSION="${TESTER_GIT_VERSION}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-44net-tester)
