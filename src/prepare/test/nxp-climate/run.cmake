cmake_minimum_required(VERSION 3.29)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${ROOT}/src/prepare/test/jdk.cmake")
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef TOKEN)
set(WORK "${ROOT}/.local/build/tmp/nxp-climate-${TOKEN}")
set(COMPONENT "${ROOT}/profiles/free-09-chinese-20221230-nxp-rc45/components/canbus")
set(SOURCE "${COMPONENT}/src/com/qinggan/canbus/service")
file(MAKE_DIRECTORY "${WORK}/classes")
execute_process(COMMAND "${JAVAC}" -J-Xmx128m --release 25 -d "${WORK}/classes"
    "${SOURCE}/ClimateModel.java" "${SOURCE}/ClimateCallbacks.java"
    "${COMPONENT}/test/ClimateModelTest.java" "${COMPONENT}/test/ClimateCallbacksTest.java"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
file(WRITE "${WORK}/compile.log" "${OUTPUT}${ERROR}")
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "NXP climate compilation: ${RESULT} ${OUTPUT} ${ERROR}; diagnostics: ${WORK}")
endif()
foreach(TEST ClimateModelTest ClimateCallbacksTest)
    execute_process(COMMAND "${JAVA}" -Xmx128m -cp "${WORK}/classes" "com.qinggan.canbus.service.${TEST}"
        TIMEOUT 15 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    file(WRITE "${WORK}/${TEST}.log" "${OUTPUT}${ERROR}")
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "NXP climate ${TEST}: ${RESULT} ${OUTPUT} ${ERROR}; diagnostics: ${WORK}")
    endif()
    message(STATUS "${OUTPUT}")
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/../java_source_dependencies.cmake")
set(POLICY_WORK "${WORK}/policy")
file(MAKE_DIRECTORY "${POLICY_WORK}/classes")
execute_process(COMMAND "${JAVAC}" -J-Xmx512m --release 25 -cp "${JAVA_SOURCE_CLASSPATH}"
    -sourcepath "${ROOT}/src/prepare/java${CP_SEPARATOR}${ROOT}/src/prepare/test/java" -d "${POLICY_WORK}/classes"
    "${ROOT}/src/prepare/java/com/voyemu/prepare/Recipes.java"
    "${ROOT}/src/prepare/java/com/voyemu/prepare/Artifacts.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/NxpNavigationTemperatureThreadTest.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/NxpCanbusReadContractTest.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/NxpCanbusPolicyTest.java"
    TIMEOUT 60 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "NXP production dispatcher compilation: ${RESULT} ${OUTPUT} ${ERROR}")
endif()
execute_process(COMMAND "${JAVA}" -Xmx256m -cp "${POLICY_WORK}/classes${CP_SEPARATOR}${JAVA_SOURCE_CLASSPATH}"
    com.voyemu.prepare.NxpCanbusPolicyTest "${ROOT}" "${POLICY_WORK}/run"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "NXP production policy/context/hash: ${RESULT} ${OUTPUT} ${ERROR}")
endif()
message(STATUS "${OUTPUT}")
execute_process(COMMAND "${JAVA}" -Xmx256m -cp "${POLICY_WORK}/classes${CP_SEPARATOR}${JAVA_SOURCE_CLASSPATH}"
    com.voyemu.prepare.NxpCanbusReadContractTest "${ROOT}" "${WORK}/read-contract"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "NXP actual provider read contract: ${RESULT} ${OUTPUT} ${ERROR}")
endif()
message(STATUS "${OUTPUT}")
# Explicit owner-supplied OEM evidence is optional; default CTest remains source-only.
if(DEFINED NXP_LAUNCHER_APK)
    execute_process(COMMAND "${JAVA}" -Xmx256m -cp "${POLICY_WORK}/classes${CP_SEPARATOR}${JAVA_SOURCE_CLASSPATH}"
        com.voyemu.prepare.NxpNavigationTemperatureThreadTest "${ROOT}" "${WORK}/navigation" "${NXP_LAUNCHER_APK}"
        TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "NXP actual OEM NavigationBar MAIN dispatcher: ${RESULT} ${OUTPUT} ${ERROR}")
    endif()
    message(STATUS "${OUTPUT}")
endif()
file(REMOVE_RECURSE "${WORK}")
