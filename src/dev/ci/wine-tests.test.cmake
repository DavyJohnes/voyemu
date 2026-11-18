cmake_minimum_required(VERSION 3.29)
# Execute the real runner with a Wine double: enforce selection completeness and failed/skip exits.
get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef token)
set(work "${root}/.local/build/jobs/wine-ci-runner-${token}")
file(MAKE_DIRECTORY "${work}/build")
set(runner "${CMAKE_CURRENT_LIST_DIR}/wine-tests.cmake")
# The executable selection is an input fixture, not inferred from the implementation source.
set(programs model phone_control host_payload android_engine windows_startup
    qnx_runtime_layout qnx_mcu_projection startup_ignition machine_host_ports android_port_lease
    zip_inventory zip_archive apk_identity apk_dex apk_digest manifest_xml window_layout
    wheel_geometry wheel_column ui_localization texture_pixels frame_transport)
foreach(name IN LISTS programs)
    file(WRITE "${work}/build/${name}_test.exe" "PE fixture\n")
endforeach()
file(WRITE "${work}/build/voyemu.exe" "PE fixture\n")
file(WRITE "${work}/wine" [=[#!/bin/sh
case "$1" in C:/voyemu-tests/*.exe) ;; *) exit 91 ;; esac
test "$TEMP" = 'C:\voyemu-tests\tmp' || exit 92
if test "$1" = C:/voyemu-tests/phone_control_test.exe; then
    case "$WINE_FIXTURE_MODE" in fail) exit 19 ;; skip) exit 77 ;; text-skip) echo 'SKIP missing input'; exit 0 ;; esac
fi
exit 0
]=])
file(CHMOD "${work}/wine" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
function(check mode success)
    set(ENV{WINE_FIXTURE_MODE} "${mode}")
    set(ENV{WINEPREFIX} "${work}/${mode}")
    file(MAKE_DIRECTORY "$ENV{WINEPREFIX}/drive_c")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DBUILD=${work}/build" "-DWINE=${work}/wine" -P "${runner}"
        TIMEOUT 30 RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(success)
        if(NOT status STREQUAL "0")
            message(FATAL_ERROR "Runner rejected complete selection: ${output}${error}")
        endif()
        file(STRINGS "$ENV{WINEPREFIX}/drive_c/voyemu-tests/results.txt" results)
        list(LENGTH results count)
        if(NOT count EQUAL 24)
            message(FATAL_ERROR "Expected all 24 Wine executions, got ${count}")
        endif()
        if(EXISTS "$ENV{WINEPREFIX}/drive_c/voyemu-tests/model_test.exe")
            file(REMOVE "$ENV{WINEPREFIX}/drive_c/voyemu-tests/model_test.exe")
        endif()
        # A reused prefix cannot silently resume an incomplete previous execution.
        execute_process(COMMAND "${CMAKE_COMMAND}" "-DBUILD=${work}/build" "-DWINE=${work}/wine" -P "${runner}"
            RESULT_VARIABLE reused OUTPUT_QUIET ERROR_QUIET)
        if(reused STREQUAL "0")
            message(FATAL_ERROR "Runner accepted a reused directory")
        endif()
    elseif(status STREQUAL "0")
        message(FATAL_ERROR "Runner accepted ${mode}")
    endif()
    message(STATUS "Wine runner contract PASS ${mode}")
endfunction()
check(complete TRUE)
check(fail FALSE)
check(skip FALSE)
check(text-skip FALSE)
file(REMOVE "${work}/build/model_test.exe")
check(missing FALSE)
file(REMOVE_RECURSE "${work}")
