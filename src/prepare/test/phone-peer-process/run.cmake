cmake_minimum_required(VERSION 3.29)
# Runs isolated peer entrypoints against actual production classes.
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef token)
set(WORK "${ROOT}/.local/build/jobs/prepare-java-phone-peer-${token}")
include("${ROOT}/src/prepare/test/jdk.cmake")
if(CMAKE_HOST_WIN32)
    set(CP_SEPARATOR ";")
else()
    set(CP_SEPARATOR ":")
endif()
set(PLATFORM "${ROOT}/.local/toolchains/java-sdk/platforms/android-34/android.jar")
set(FIXTURE "${ROOT}/src/prepare/test/fixtures/phone-peer-process")
set(COMPONENT "${ROOT}/profiles/free-09-chinese-20260106-6.0.3/components/bluetooth/src")
set(TESTS "${ROOT}/src/prepare/test/java/com/voyemu/prepare")
foreach(required IN ITEMS "${JAVA}" "${JAVAC}" "${PLATFORM}"
        "${COMPONENT}/Ivt8BTAdapter.java" "${FIXTURE}/fixture/PhonePeerProcessProbe.java"
        "${FIXTURE}/fixture/A2dpOriginalConsumerProbe.java"
        "${FIXTURE}/fixture/SubscriberNumberContractProbe.java")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "Peer test prerequisite missing: ${required}")
    endif()
endforeach()
if(EXISTS "${WORK}" OR IS_SYMLINK "${WORK}")
    message(FATAL_ERROR "Peer test WORK must be fresh: ${WORK}")
endif()
file(MAKE_DIRECTORY "${WORK}")
function(peer_compile name classpath)
    foreach(input IN LISTS ARGN)
        file(SHA256 "${input}" sha)
        file(APPEND "${WORK}/provenance.sha256" "${sha}  ${input}\n")
    endforeach()
    set(output "${WORK}/${name}")
    file(MAKE_DIRECTORY "${output}")
    execute_process(COMMAND "${JAVAC}" -J-Xmx512m -J-XX:ActiveProcessorCount=1 --release 25 -encoding UTF-8 -proc:none
        -classpath "${classpath}" -d "${output}" ${ARGN}
        WORKING_DIRECTORY "${ROOT}" TIMEOUT 60
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    file(WRITE "${WORK}/${name}-compile.log" "${stdout}${stderr}")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Peer ${name} compilation failed: ${result}\n${stdout}\n${stderr}")
    endif()
endfunction()
file(GLOB_RECURSE boundary_sources
    "${FIXTURE}/android/*.java" "${FIXTURE}/org/*.java" "${FIXTURE}/com/*.java")
peer_compile(boundary "${PLATFORM}" ${boundary_sources}
    "${ROOT}/src/prepare/java/com/voyemu/prepare/Json.java")
file(GLOB_RECURSE production_sources "${COMPONENT}/*.java")
# The OEM-shaped test bases of fixture/oem carry the inherited state and lifecycle; the
# compile-only declarations of stub-cls have signatures only and must not replace them here.
list(FILTER production_sources EXCLUDE REGEX "/stub-cls/com/qinggan/bluetooth/(profile/(BTAdapter|BluetoothCall|A2dp|BTDevice|SimpleHfpCallback|SimplePbapCallback)|BTDeviceManager|bean/(BTContactItem|BTPhoneNumber))\\.java$")
file(GLOB_RECURSE oem_sources "${FIXTURE}/fixture/oem/*.java")
peer_compile(production "${WORK}/boundary${CP_SEPARATOR}${PLATFORM}"
    ${production_sources} ${oem_sources} "${FIXTURE}/fixture/PhonePeerProcessProbe.java")
peer_compile(consumer "${WORK}/production${CP_SEPARATOR}${WORK}/boundary${CP_SEPARATOR}${PLATFORM}"
    "${FIXTURE}/fixture/PeerConsumerFailureProbe.java"
    "${FIXTURE}/fixture/A2dpOriginalConsumerProbe.java"
    "${FIXTURE}/fixture/SubscriberNumberContractProbe.java")
peer_compile(harness "${PLATFORM}"
    "${TESTS}/SyntheticPhonePeerConsumerIsolationTest.java"
    "${TESTS}/SyntheticPhonePeerSubscriptionBoundsTest.java"
    "${TESTS}/SyntheticPhonePeerAndroidNineFlagsTest.java"
    "${TESTS}/SyntheticPhonePeerTwoProcessTest.java"
    "${TESTS}/SyntheticPhonePeerA2dpConsumerTest.java"
    "${TESTS}/SyntheticPhonePeerSubscriberNumberTest.java"
    "${ROOT}/src/prepare/java/com/voyemu/prepare/Json.java")
function(peer_run name class)
    file(MAKE_DIRECTORY "${WORK}/${name}/files" "${WORK}/${name}/tmp")
    execute_process(COMMAND "${JAVA}" -Xmx256m -XX:ActiveProcessorCount=1
        "-Dphone.peer.test.files=${WORK}/${name}/files" "-Djava.io.tmpdir=${WORK}/${name}/tmp"
        -cp "${WORK}/harness" "com.voyemu.prepare.${class}" ${ARGN}
        WORKING_DIRECTORY "${ROOT}" TIMEOUT 45
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    file(WRITE "${WORK}/${name}.log" "${stdout}${stderr}")
    if(NOT result EQUAL 0)
        message(STATUS "${name}: failed ${result}\n${stdout}\n${stderr}")
        set(FAILED ${FAILED} "${name}" PARENT_SCOPE)
    else()
        message(STATUS "${name}: ${stdout}")
    endif()
endfunction()
set(FAILED)
foreach(scenario sameprocess diagnostics early immediate persistence death malformed:key malformed:sync malformed:events)
    string(REPLACE ":" "-" name "${scenario}")
    peer_run("${name}" SyntheticPhonePeerTwoProcessTest
        "${WORK}/production" "${WORK}/boundary" "${PLATFORM}" "${scenario}")
endforeach()
peer_run(a2dp SyntheticPhonePeerA2dpConsumerTest
    "${WORK}/production" "${WORK}/boundary" "${WORK}/consumer" "${PLATFORM}")
peer_run(subscriber SyntheticPhonePeerSubscriberNumberTest
    "${WORK}/production" "${WORK}/boundary" "${WORK}/consumer" "${PLATFORM}")
peer_run(android-nine-flags SyntheticPhonePeerAndroidNineFlagsTest
    "${WORK}/production" "${WORK}/boundary" "${PLATFORM}")
peer_run(subscription-bounds SyntheticPhonePeerSubscriptionBoundsTest
    "${WORK}/production" "${WORK}/boundary" "${WORK}/boundary" "${PLATFORM}")
foreach(stage hfpconnection ibtconnection hfpcall)
    peer_run("consumer-isolation-${stage}" SyntheticPhonePeerConsumerIsolationTest
        "${WORK}/production" "${WORK}/boundary" "${WORK}/consumer" "${PLATFORM}" "${stage}")
endforeach()
if(FAILED)
    message(FATAL_ERROR "Peer fixture failures: ${FAILED}; diagnostics: ${WORK}")
endif()
set(SUMMARY "${WORK}-summary")
if(NOT WORK STREQUAL "${ROOT}/.local/build/jobs/prepare-java-phone-peer-${token}"
        OR IS_SYMLINK "${WORK}" OR EXISTS "${SUMMARY}" OR IS_SYMLINK "${SUMMARY}")
    message(FATAL_ERROR "Owned success cleanup guard failed: ${WORK}")
endif()
file(MAKE_DIRECTORY "${SUMMARY}")
file(GLOB logs "${WORK}/*.log")
file(COPY ${logs} DESTINATION "${SUMMARY}")
file(GLOB_RECURSE artifacts "${WORK}/*.class")
foreach(artifact IN LISTS artifacts)
    file(SHA256 "${artifact}" sha)
    file(RELATIVE_PATH name "${WORK}" "${artifact}")
    file(APPEND "${SUMMARY}/artifacts.sha256" "${sha}  ${name}\n")
endforeach()
file(COPY "${WORK}/provenance.sha256" DESTINATION "${SUMMARY}")
foreach(input IN ITEMS "${JAVA}" "${JAVAC}" "${PLATFORM}" "${CMAKE_CURRENT_LIST_FILE}")
    file(SHA256 "${input}" sha)
    file(APPEND "${SUMMARY}/provenance.sha256" "${sha}  ${input}\n")
endforeach()
file(WRITE "${SUMMARY}/summary.txt"
    "exit=0\ntwo_process_cases=9\na2dp_cases=1\nsubscriber_cases=1\nandroid_nine_flags_cases=1\nsubscription_bounds_cases=1\nconsumer_isolation_cases=3\nwork=${WORK}\n")
file(REMOVE_RECURSE "${WORK}")
if(EXISTS "${WORK}" OR IS_SYMLINK "${WORK}")
    message(FATAL_ERROR "Owned WORK cleanup failed: ${WORK}")
endif()
message(STATUS "Peer fixture checks passed: all nine two-process branches, a2dp, subscriber, Android9 flags, subscription bounds, consumer isolation; summary ${SUMMARY}")
