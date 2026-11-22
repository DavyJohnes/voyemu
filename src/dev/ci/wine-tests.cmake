cmake_minimum_required(VERSION 3.29)
# Run portable PE checks under Wine. Windows reparse fixtures, listener owner PID and
# creator DACL contracts remain mandatory in the native Windows CTest lane.
# model remains required here for the unchanged state_replace incarnation guards.
# C: holds ordinary files; Wine's Z: mapping exposes reparse points to identity guards.
if(NOT IS_DIRECTORY "${BUILD}" OR NOT EXISTS "${WINE}" OR NOT IS_DIRECTORY "$ENV{WINEPREFIX}/drive_c")
    message(FATAL_ERROR "BUILD, WINE and an initialized private WINEPREFIX are required")
endif()
set(work "$ENV{WINEPREFIX}/drive_c/voyemu-tests")
if(EXISTS "${work}")
    message(FATAL_ERROR "A fresh Wine test directory is required: ${work}")
endif()
set(tests phone_control host_payload android_engine windows_startup
    qnx_runtime_layout qnx_mcu_projection machine_host_ports android_port_lease
    zip_inventory zip_archive apk_identity apk_dex apk_digest manifest_xml window_layout
    wheel_geometry wheel_column ui_localization texture_pixels frame_transport)
set(programs ${tests} model voyemu)
# Validate the whole selection before running anything; a missing executable is never a skip.
foreach(name IN LISTS programs)
    if(NOT EXISTS "${BUILD}/${name}_test.exe" AND NOT name STREQUAL "voyemu")
        message(FATAL_ERROR "Missing Windows executable: ${name}_test.exe")
    endif()
endforeach()
if(NOT EXISTS "${BUILD}/voyemu.exe")
    message(FATAL_ERROR "Missing Windows executable: voyemu.exe")
endif()
file(MAKE_DIRECTORY "${work}/tmp")
foreach(name IN LISTS programs)
    if(name STREQUAL "voyemu")
        set(file voyemu.exe)
    else()
        set(file "${name}_test.exe")
    endif()
    if(IS_SYMLINK "${BUILD}/${file}")
        message(FATAL_ERROR "Ordinary PE file required: ${file}")
    endif()
    file(COPY_FILE "${BUILD}/${file}" "${work}/${file}")
endforeach()
# Windows APIs and child processes must also use C: for their temporary files.
set(ENV{TEMP} "C:\\voyemu-tests\\tmp")
set(ENV{TMP} "$ENV{TEMP}")
function(run name executable)
    execute_process(COMMAND "${WINE}" "C:/voyemu-tests/${executable}" ${ARGN}
        WORKING_DIRECTORY "${work}" TIMEOUT 180 RESULT_VARIABLE result
        OUTPUT_VARIABLE output ERROR_VARIABLE error)
    file(WRITE "${work}/${name}.log" "${output}${error}")
    if(NOT result STREQUAL "0" OR "${output}\n${error}" MATCHES "(^|\n)(SKIP|Skipped)([ :]|$)")
        message(FATAL_ERROR "Wine test ${name} failed (${result}):\n${output}${error}")
    endif()
    file(APPEND "${work}/results.txt" "PASS ${name}\n")
    message(STATUS "Wine PASS ${name}")
endfunction()
run(help voyemu.exe --help)
run(version voyemu.exe --version)
foreach(name IN LISTS tests)
    run("${name}" "${name}_test.exe")
endforeach()
# This exercises unchanged ftCreationTime/incarnation checks through actual Windows APIs.
run(state_replace model_test.exe --replace-regression "C:/voyemu-tests/tmp")
