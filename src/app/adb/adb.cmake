# Builds voyemu_embedded_adb from the Voyemu host sources in this directory and the AOSP
# sources in third_party/aosp_adb. Unmodified AOSP files are verified against their SHA-256
# pins in sources.json; modified AOSP files record only their upstream revision and base SHA-256.
set(voyemu_adb_root "${CMAKE_CURRENT_LIST_DIR}")
file(REAL_PATH "${CMAKE_CURRENT_LIST_DIR}/../third_party/aosp_adb" voyemu_aosp_adb_root)
set(voyemu_aosp_adb_manifest "${voyemu_aosp_adb_root}/sources.json")
file(READ "${voyemu_aosp_adb_manifest}" voyemu_aosp_adb_inventory)
string(JSON voyemu_aosp_adb_schema GET "${voyemu_aosp_adb_inventory}" schema)
string(JSON voyemu_aosp_adb_count LENGTH "${voyemu_aosp_adb_inventory}" files)
if(NOT voyemu_aosp_adb_schema EQUAL 2 OR voyemu_aosp_adb_count LESS 1)
    message(FATAL_ERROR "Unsupported AOSP ADB source manifest")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${voyemu_aosp_adb_manifest}")

set(voyemu_aosp_adb_inputs)
set(voyemu_adb_sources "${voyemu_adb_root}/adb_auth.cpp" "${voyemu_adb_root}/adb_host.cpp"
    "${voyemu_adb_root}/adb_loopback.cpp" "${voyemu_adb_root}/adb_loopback_${voyemu_host_backend}.cpp")
if(WIN32)
    list(APPEND voyemu_adb_sources "${voyemu_adb_root}/adb_crt_windows.cpp"
        "${voyemu_adb_root}/adb_socket_windows.cpp")
endif()
math(EXPR voyemu_aosp_adb_last "${voyemu_aosp_adb_count}-1")
foreach(index RANGE ${voyemu_aosp_adb_last})
    string(JSON path GET "${voyemu_aosp_adb_inventory}" files ${index} path)
    if(NOT path MATCHES "^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*$" OR
       path MATCHES "(^|/)[.][.]?(/|$)" OR path IN_LIST voyemu_aosp_adb_inputs)
        message(FATAL_ERROR "Unsafe or duplicate AOSP ADB input: ${path}")
    endif()
    list(APPEND voyemu_aosp_adb_inputs "${path}")
    set(input "${voyemu_aosp_adb_root}/${path}")
    if(NOT EXISTS "${input}" OR IS_DIRECTORY "${input}" OR IS_SYMLINK "${input}")
        message(FATAL_ERROR "Missing or unsafe AOSP ADB input: ${path}")
    endif()
    file(REAL_PATH "${input}" resolved)
    if(NOT resolved STREQUAL input)
        message(FATAL_ERROR "Linked AOSP ADB input: ${path}")
    endif()
    string(JSON modified ERROR_VARIABLE modified_missing GET "${voyemu_aosp_adb_inventory}" files ${index} modified)
    if(NOT modified)
        string(JSON expected_sha GET "${voyemu_aosp_adb_inventory}" files ${index} sha256)
        string(JSON expected_bytes GET "${voyemu_aosp_adb_inventory}" files ${index} bytes)
        file(SIZE "${input}" actual_bytes)
        file(SHA256 "${input}" actual_sha)
        if(NOT actual_bytes EQUAL expected_bytes OR NOT actual_sha STREQUAL expected_sha)
            message(FATAL_ERROR "AOSP ADB input differs from its upstream pin: ${path}")
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
    endif()
    if(WIN32 AND path MATCHES "^(adb/sysdeps_unix|base/file)[.]cpp$")
        continue()
    endif()
    if(NOT WIN32 AND path MATCHES "^base/utf8[.]cpp$")
        continue()
    endif()
    if(path MATCHES "[.]cpp$")
        list(APPEND voyemu_adb_sources "${input}")
    endif()
endforeach()

function(voyemu_configure_embedded_adb target)
    target_include_directories(${target} PRIVATE "${voyemu_adb_root}/compat"
        "${voyemu_aosp_adb_root}/adb" "${voyemu_aosp_adb_root}/adb/fdevent"
        "${voyemu_aosp_adb_root}/base/include" "${voyemu_aosp_adb_root}/base"
        "${voyemu_aosp_adb_root}/cutils/libcutils/include" "${voyemu_aosp_adb_root}/log/liblog/include")
    target_compile_definitions(${target} PRIVATE ADB_HOST=1 ADB_VERSION=41 _FILE_OFFSET_BITS=64 _GNU_SOURCE)
    if(WIN32)
        target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX SHUT_WR=SD_SEND SHUT_RDWR=SD_BOTH)
        target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
    endif()
    target_compile_options(${target} PRIVATE -Oz -g0 -fvisibility=hidden -fno-ident
        "-ffile-prefix-map=${voyemu_aosp_adb_root}=" "-ffile-prefix-map=${voyemu_adb_root}="
        -Wno-deprecated-declarations -Wno-deprecated-anon-enum-enum-conversion -Wno-unused-result)
endfunction()

add_library(voyemu_embedded_adb STATIC EXCLUDE_FROM_ALL ${voyemu_adb_sources})
voyemu_configure_embedded_adb(voyemu_embedded_adb)
target_include_directories(voyemu_embedded_adb PUBLIC "${voyemu_adb_root}")
if(WIN32)
    target_link_libraries(voyemu_embedded_adb PRIVATE ws2_32)
    target_link_options(voyemu_embedded_adb INTERFACE "LINKER:--gc-sections")
elseif(APPLE)
    # Discard unused Android-only library routines.
    target_link_options(voyemu_embedded_adb INTERFACE "LINKER:-dead_strip")
else()
    target_compile_options(voyemu_embedded_adb PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(voyemu_embedded_adb INTERFACE "LINKER:--gc-sections")
endif()

if(BUILD_TESTING)
    add_executable(voyemu_embedded_adb_loopback_test "${voyemu_adb_root}/adb_loopback.test.cpp"
        "${voyemu_adb_root}/adb_host.test.cpp")
    voyemu_configure_embedded_adb(voyemu_embedded_adb_loopback_test)
    target_link_libraries(voyemu_embedded_adb_loopback_test PRIVATE voyemu_embedded_adb)
    set(voyemu_adb_cases target-selection shell-stream upload-eof cancel-isolation
        auth-unauthorized missing-shell-v2 malformed-magic malformed-size
        malformed-checksum truncated-header truncated-payload stls reconnect-generation
        shutdown-idle shutdown-partial shutdown-write legacy-checksum control-services
        packet-deadline transport-close-io transport-write-deadline operation-deadline fenced-ownership
        device-event-budget packet-backlog-bound event-replacement-admission
        shutdown-device-retry control-overflow-terminal)
    if(TARGET voyemu_core)
        target_compile_definitions(voyemu_embedded_adb_loopback_test PRIVATE VOYEMU_ADB_ADAPTER_TEST)
        target_link_libraries(voyemu_embedded_adb_loopback_test PRIVATE voyemu_core)
        list(APPEND voyemu_adb_cases reboot-generation)
    endif()
    if(NOT WIN32)
        list(APPEND voyemu_adb_cases packet-eintr-deadline malformed-ack-leak socket-backlog-bound)
    endif()
    foreach(case IN LISTS voyemu_adb_cases)
        if(WIN32)
            voyemu_add_test(embedded_adb_${case} LABELS host TIMEOUT 20 COMMAND voyemu_embedded_adb_loopback_test "${case}")
        else()
            voyemu_add_test(embedded_adb_${case} LABELS host TIMEOUT 20 COMMAND "${CMAKE_COMMAND}"
                "-Dtest_executable=$<TARGET_FILE:voyemu_embedded_adb_loopback_test>"
                "-Dtest_case=${case}" -P "${voyemu_adb_root}/adb_test.cmake")
        endif()
    endforeach()
endif()
