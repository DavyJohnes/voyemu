cmake_minimum_required(VERSION 3.22)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${ROOT}/src/prepare/test/jdk.cmake")
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef TOKEN)
set(WORK "${ROOT}/.local/build/tmp/startup-audio-session-${TOKEN}")
file(MAKE_DIRECTORY "${WORK}")
set(DOUBLES "${CMAKE_CURRENT_LIST_DIR}/android/content/Context.java"
    "${CMAKE_CURRENT_LIST_DIR}/android/content/SharedPreferences.java"
    "${CMAKE_CURRENT_LIST_DIR}/android/media/AudioManager.java"
    "${CMAKE_CURRENT_LIST_DIR}/com/voyemu/output/StartupAudioSessionTest.java")
set(PROFILES passion-11-russian-20260403-2.1.4.80 free-09-chinese-20221230-nxp-rc45)
set(STREAMS 12 11)
foreach(INDEX RANGE 0 1)
    list(GET PROFILES ${INDEX} PROFILE)
    list(GET STREAMS ${INDEX} COUNT)
    set(CLASSES "${WORK}/${PROFILE}")
    file(MAKE_DIRECTORY "${CLASSES}")
    execute_process(COMMAND "${JAVAC}" -J-Xmx128m --release 25 -d "${CLASSES}" ${DOUBLES}
        "${ROOT}/profiles/${PROFILE}/components/bootstrap/src/com/voyemu/output/StartupAudioPolicy.java"
        TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "${PROFILE} compilation: ${RESULT}\n${OUTPUT}\n${ERROR}")
    endif()
    execute_process(COMMAND "${JAVA}" -Xmx128m -cp "${CLASSES}" com.voyemu.output.StartupAudioSessionTest "${COUNT}"
        TIMEOUT 15 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    file(WRITE "${WORK}/${PROFILE}.log" "${OUTPUT}${ERROR}")
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "${PROFILE} test: ${RESULT}\n${OUTPUT}\n${ERROR}")
    endif()
    message(STATUS "${PROFILE}: ${OUTPUT}")
endforeach()
