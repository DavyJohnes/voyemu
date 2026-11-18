cmake_minimum_required(VERSION 3.29)
# Actual request + not-ready producer, with fail-fast doubles for unused Android lifecycle paths.
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${ROOT}/src/prepare/test/jdk.cmake")
if(NOT EXISTS "${CAN_CONSUMER}")
    message(FATAL_ERROR "Built can_observation_test required")
endif()
set(PLATFORM "${ROOT}/.local/toolchains/java-sdk/platforms/android-34/android.jar")
set(FIXTURE "${ROOT}/src/prepare/test/canbus-observation")
set(JSON_FIXTURE "${ROOT}/src/prepare/test/fixtures/phone-peer-process/org/json")
if(WIN32)
    set(CP_SEPARATOR ";")
else()
    set(CP_SEPARATOR ":")
endif()
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef TOKEN)
set(WORK "${ROOT}/.local/build/jobs/canbus-observation-${TOKEN}")
file(GLOB_RECURSE FIXTURES "${FIXTURE}/*.java")
set(SOURCE "${ROOT}/profiles/common/components/canbus/src/com/qinggan/canbus/service")
set(CLASSES "${WORK}/classes")
file(MAKE_DIRECTORY "${CLASSES}")
execute_process(COMMAND "${JAVAC}" -J-XX:ActiveProcessorCount=1 -J-Xmx128m --release 25 -encoding UTF-8 -proc:none
    -cp "${PLATFORM}" -d "${CLASSES}" ${FIXTURES}
    "${ROOT}/src/prepare/java/com/voyemu/prepare/Json.java"
    "${JSON_FIXTURE}/JSONObject.java" "${JSON_FIXTURE}/JSONArray.java" "${JSON_FIXTURE}/JSONException.java"
    "${SOURCE}/CanObservationRequest.java" "${SOURCE}/RegistrationService.java"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Compilation: ${RESULT}\n${OUTPUT}\n${ERROR}; diagnostics: ${WORK}")
endif()
set(PRODUCED "${WORK}/observation.json")
execute_process(COMMAND "${JAVA}" -XX:ActiveProcessorCount=1 -Xmx128m
    -cp "${CLASSES}${CP_SEPARATOR}${PLATFORM}" com.qinggan.canbus.service.CanObservationProducerProbe "${PRODUCED}"
    TIMEOUT 15 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Producer: ${RESULT}\n${OUTPUT}\n${ERROR}; diagnostics: ${WORK}")
endif()
message(STATUS "${OUTPUT}")
execute_process(COMMAND "${CAN_CONSUMER}" "${PRODUCED}"
    TIMEOUT 15 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "C++ consumer: ${RESULT}\n${OUTPUT}\n${ERROR}; diagnostics: ${WORK}")
endif()
message(STATUS "${OUTPUT}")
file(REMOVE_RECURSE "${WORK}")
