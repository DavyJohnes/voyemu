cmake_minimum_required(VERSION 3.29)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${ROOT}/src/prepare/test/jdk.cmake")
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef TOKEN)
set(WORK "${ROOT}/.local/build/jobs/qinggan09-cluster-theme-${TOKEN}")
set(SOURCE "${ROOT}/profiles/common/components/canbus/src/com/qinggan/canbus/service")
file(MAKE_DIRECTORY "${WORK}/classes")
execute_process(COMMAND "${JAVAC}" -J-Xmx128m --release 25 -d "${WORK}/classes"
    "${ROOT}/src/prepare/test/qinggan09-startup-audio/android/content/SharedPreferences.java"
    "${SOURCE}/ClusterThemeStore.java"
    "${ROOT}/src/prepare/test/java/com/qinggan/canbus/service/ClusterThemeStoreTest.java"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "A9 theme compilation: ${RESULT}\n${OUTPUT}\n${ERROR}; diagnostics: ${WORK}")
endif()
execute_process(COMMAND "${JAVA}" -Xmx128m -cp "${WORK}/classes" com.qinggan.canbus.service.ClusterThemeStoreTest
    TIMEOUT 15 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "A9 theme persistence: ${RESULT}\n${OUTPUT}\n${ERROR}; diagnostics: ${WORK}")
endif()
message(STATUS "${OUTPUT}")
file(REMOVE_RECURSE "${WORK}")
