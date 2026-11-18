cmake_minimum_required(VERSION 3.22)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${ROOT}/src/prepare/test/jdk.cmake")
execute_process(COMMAND "${JAVA}" "${CMAKE_CURRENT_LIST_DIR}/Run.java" "${ROOT}"
    TIMEOUT 120 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "NXP phone contracts: ${RESULT} ${OUTPUT} ${ERROR}")
endif()
message(STATUS "${OUTPUT}")
