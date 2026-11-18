cmake_minimum_required(VERSION 3.22)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
include("${ROOT}/src/prepare/test/jdk.cmake")
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef TOKEN)
set(WORK "${ROOT}/.local/build/tmp/window-routing-${TOKEN}")
file(MAKE_DIRECTORY "${WORK}")
file(GLOB_RECURSE DOUBLES "${CMAKE_CURRENT_LIST_DIR}/*.java")
set(ROUTER "${ROOT}/profiles/free-09-chinese-20260106-6.0.3/patches/launcher-display-routing/src/com/voyemu/output/WindowRouter.java"
    "${ROOT}/profiles/free-09-chinese-20260106-6.0.3/patches/launcher-display-routing/src/com/voyemu/output/DisplayTasks.java"
    "${ROOT}/profiles/free-09-chinese-20260106-6.0.3/patches/ime-display-routing/src/com/voyemu/input/ImeDisplayRouter.java")
execute_process(COMMAND "${JAVAC}" -J-Xmx128m --release 25 -d "${WORK}" ${DOUBLES} ${ROUTER}
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Window routing compilation: ${RESULT} ${OUTPUT} ${ERROR}")
endif()
execute_process(COMMAND "${JAVA}" -Xmx128m -cp "${WORK}" com.voyemu.output.WindowRouterTest
    TIMEOUT 15 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
file(WRITE "${WORK}/result.log" "${OUTPUT}${ERROR}")
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Window routing behavior: ${RESULT} ${OUTPUT} ${ERROR}")
endif()
message(STATUS "${OUTPUT}")

execute_process(COMMAND "${JAVA}" -Xmx128m -cp "${WORK}" com.voyemu.output.DisplayRoutingTest
    TIMEOUT 15 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
file(WRITE "${WORK}/display-routing-result.log" "${OUTPUT}${ERROR}")
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Display routing behavior: ${RESULT} ${OUTPUT} ${ERROR}")
endif()
message(STATUS "${OUTPUT}")
