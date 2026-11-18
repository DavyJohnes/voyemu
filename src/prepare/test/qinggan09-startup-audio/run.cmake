cmake_minimum_required(VERSION 3.22)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${ROOT}/src/prepare/test/jdk.cmake")
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef TOKEN)
set(WORK "${ROOT}/.local/build/tmp/qinggan09-startup-audio-session-${TOKEN}")
file(MAKE_DIRECTORY "${WORK}/classes")
if(NOT DEFINED COMPONENTS)
    set(COMPONENTS "${ROOT}/profiles/free-09-chinese-20260106-6.0.3/components")
endif()
file(GLOB_RECURSE DOUBLES "${CMAKE_CURRENT_LIST_DIR}/android/*.java" "${CMAKE_CURRENT_LIST_DIR}/org/*.java")
execute_process(COMMAND "${JAVAC}" -J-Xmx128m --release 25 -d "${WORK}/classes"
    ${DOUBLES} "${CMAKE_CURRENT_LIST_DIR}/StartupAudioPolicyTest.java" "${CMAKE_CURRENT_LIST_DIR}/AudioStartupProviderTest.java"
    "${ROOT}/profiles/free-09-chinese-20260106-6.0.3/components/audiopolicy/src/com/qinggan/audiopolicy/AudioStartupProvider.java"
    "${ROOT}/profiles/free-09-chinese-20260106-6.0.3/components/audiopolicy/src/com/qinggan/audiopolicy/StartupAudioPolicy.java"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
file(WRITE "${WORK}/compile.log" "${OUTPUT}${ERROR}")
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "A9 startup audio compilation: ${RESULT}\n${OUTPUT}\n${ERROR}")
endif()
execute_process(COMMAND "${JAVA}" -Xmx128m -cp "${WORK}/classes" com.qinggan.audiopolicy.StartupAudioPolicyTest "${COMPONENTS}"
    TIMEOUT 15 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
file(WRITE "${WORK}/result.log" "${OUTPUT}${ERROR}")
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "A9 startup audio test: ${RESULT}\n${OUTPUT}\n${ERROR}")
endif()
message(STATUS "${OUTPUT}")
execute_process(COMMAND "${JAVA}" -Xmx128m -cp "${WORK}/classes" com.qinggan.audiopolicy.AudioStartupProviderTest "${COMPONENTS}"
    TIMEOUT 15 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
file(WRITE "${WORK}/provider-result.log" "${OUTPUT}${ERROR}")
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "A9 audio provider test: ${RESULT}\n${OUTPUT}\n${ERROR}")
endif()
message(STATUS "${OUTPUT}")
