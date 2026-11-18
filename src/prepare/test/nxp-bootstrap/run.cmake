cmake_minimum_required(VERSION 3.22)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${ROOT}/src/prepare/test/jdk.cmake")
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef TOKEN)
set(WORK "${ROOT}/.local/build/tmp/nxp-bootstrap-${TOKEN}")
set(PROFILE "${ROOT}/profiles/free-09-chinese-20221230-nxp-rc45/components/bootstrap/src/com/voyemu/output")
file(MAKE_DIRECTORY "${WORK}/service" "${WORK}/audio")
file(GLOB_RECURSE DOUBLES "${CMAKE_CURRENT_LIST_DIR}/*.java")
execute_process(COMMAND "${JAVAC}" -J-Xmx128m --release 25 -d "${WORK}/service" ${DOUBLES}
    "${CMAKE_CURRENT_LIST_DIR}/../startup-audio-session/android/content/SharedPreferences.java" "${PROFILE}/SecondScreenService.java"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "NXP service compilation: ${RESULT} ${OUTPUT} ${ERROR}")
endif()
execute_process(COMMAND "${JAVA}" -Xmx128m -cp "${WORK}/service" com.voyemu.output.NxpBootstrapTest
    TIMEOUT 15 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
file(WRITE "${WORK}/service.log" "${OUTPUT}${ERROR}")
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "NXP service behavior: ${RESULT} ${OUTPUT} ${ERROR}")
endif()
message(STATUS "${OUTPUT}")
set(AUDIO "${CMAKE_CURRENT_LIST_DIR}/../startup-audio-session")
execute_process(COMMAND "${JAVAC}" -J-Xmx128m --release 25 -d "${WORK}/audio"
    "${AUDIO}/android/content/Context.java" "${AUDIO}/android/content/SharedPreferences.java" "${AUDIO}/android/media/AudioManager.java"
    "${AUDIO}/com/voyemu/output/StartupAudioSessionTest.java" "${PROFILE}/StartupAudioPolicy.java"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "NXP original audio compilation: ${RESULT} ${OUTPUT} ${ERROR}")
endif()
execute_process(COMMAND "${JAVA}" -Xmx128m -cp "${WORK}/audio" com.voyemu.output.StartupAudioSessionTest 11
    TIMEOUT 15 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
file(WRITE "${WORK}/audio.log" "${OUTPUT}${ERROR}")
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "NXP original audio behavior: ${RESULT} ${OUTPUT} ${ERROR}")
endif()
message(STATUS "${OUTPUT}")
