cmake_minimum_required(VERSION 3.29)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${ROOT}/src/prepare/test/jdk.cmake")
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef TOKEN)
set(WORK "${ROOT}/.local/build/tmp/nxp-wheel-${TOKEN}")
file(MAKE_DIRECTORY "${WORK}/classes")
include("${CMAKE_CURRENT_LIST_DIR}/../java_source_dependencies.cmake")
execute_process(COMMAND "${JAVAC}" -J-Xmx256m --release 25 -cp "${JAVA_SOURCE_CLASSPATH}" -sourcepath "${ROOT}/src/prepare/java" -d "${WORK}/classes"
    "${ROOT}/src/prepare/java/com/voyemu/prepare/ComponentSources.java"
    "${ROOT}/src/prepare/java/com/voyemu/prepare/SourceEdits.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/NxpWheelSourcesTest.java"
    TIMEOUT 45 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "NXP wheel source test compilation: ${RESULT} ${OUTPUT} ${ERROR}; ${WORK}")
endif()
execute_process(COMMAND "${JAVA}" -Xmx256m -cp "${WORK}/classes${CP_SEPARATOR}${JAVA_SOURCE_CLASSPATH}" com.voyemu.prepare.NxpWheelSourcesTest "${ROOT}" "${WORK}/source"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "NXP wheel source pipeline: ${RESULT} ${OUTPUT} ${ERROR}; ${WORK}")
endif()
message(STATUS "${OUTPUT}")
file(REMOVE_RECURSE "${WORK}")
