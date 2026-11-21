cmake_minimum_required(VERSION 3.29)
# Shared CanBus component semantic checks, model ACC and component behavior checks and retained manifest
# self-test; the cluster ABI check needs the imported QGAPI and the OEM API parity the imported donors.
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${ROOT}/src/prepare/test/jdk.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../java_source_dependencies.cmake")
if(TEST_OEM AND NOT EXISTS "${ROOT}/.local/profiles/free-11-chinese-20260731-7.3.1/originals/framework/QGAPI.jar")
    message(FATAL_ERROR "OEM cluster ABI regression requires QGAPI.jar")
endif()
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef TOKEN)
set(WORK "${ROOT}/.local/build/jobs/canbus-${TOKEN}")
set(COMPONENT "${ROOT}/profiles/common/components/canbus/src")
set(SERVICE "${COMPONENT}/com/qinggan/canbus/service")
set(TESTS "${ROOT}/src/prepare/test/java/com/qinggan/canbus/service")
function(run_java CLASSPATH CLASS)
    execute_process(COMMAND "${JAVA}" -Xmx512m -cp "${CLASSPATH}" "${CLASS}" ${ARGN}
        WORKING_DIRECTORY "${ROOT}" TIMEOUT 120 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "${CLASS}: ${RESULT}\n${OUTPUT}\n${ERROR}\ndiagnostics: ${WORK}")
    endif()
    message(STATUS "${CLASS}: passed ${OUTPUT}")
endfunction()
function(compile OUTPUT_DIRECTORY)
    cmake_parse_arguments(PARSE_ARGV 1 COMPILE "" "CLASSPATH" "")
    set(OPTIONS)
    if(DEFINED COMPILE_CLASSPATH)
        string(REPLACE ";" "\\;" CLASSPATH "${COMPILE_CLASSPATH}")
        list(APPEND OPTIONS -cp "${CLASSPATH}")
    endif()
    file(MAKE_DIRECTORY "${OUTPUT_DIRECTORY}")
    execute_process(COMMAND "${JAVAC}" -J-Xmx512m --release 25 -encoding UTF-8 -proc:none -nowarn
        -d "${OUTPUT_DIRECTORY}" ${OPTIONS} ${COMPILE_UNPARSED_ARGUMENTS}
        TIMEOUT 300 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "Compilation ${OUTPUT_DIRECTORY}: ${RESULT}\n${OUTPUT}\n${ERROR}")
    endif()
endfunction()
file(GLOB_RECURSE FIXTURES "${ROOT}/src/prepare/test/fixtures/canbus-hfp/*.java")
set(SEMANTIC HfpPhoneInfoValuesTest HfpPhoneInfoBridgeTest HfpPhoneInfoPrivacyTest IpkFramesTest IpkLinkTest
    IpkInfoSyncTest PhoneDeliveryDiagnosticsTest ReconnectTest QnxWireDecodeTest)
set(SOURCES ${FIXTURES})
foreach(NAME CanObservationJournal HfpPhoneInfoBridge HfpPhoneInfoValues IpkFrames IpkInfoSync IpkLink
        PhoneDeliveryDiagnostics)
    list(APPEND SOURCES "${SERVICE}/${NAME}.java")
endforeach()
foreach(NAME IN LISTS SEMANTIC)
    list(APPEND SOURCES "${TESTS}/${NAME}.java")
endforeach()
compile("${WORK}/semantic" ${SOURCES})
foreach(NAME IN LISTS SEMANTIC)
    run_java("${WORK}/semantic" "com.qinggan.canbus.service.${NAME}" "${ROOT}" "${ROOT}")
endforeach()
file(GLOB_RECURSE MODEL_FIXTURES "${ROOT}/src/prepare/test/fixtures/canbus-model/*.java")
compile("${WORK}/acc" ${MODEL_FIXTURES} "${SERVICE}/CanModelBridge.java" "${SERVICE}/CanModelClient.java"
    "${TESTS}/CanBusAccStatusTest.java")
run_java("${WORK}/acc" com.qinggan.canbus.service.CanBusAccStatusTest)
# The component with the qinggan09 theme variant and with each platform vehicle catalogue; its model
# socket is the canbus-model double.
file(GLOB_RECURSE COMPONENT_FIXTURES "${ROOT}/src/prepare/test/fixtures/canbus-component/*.java")
set(COMPONENT_SOURCES)
foreach(NAME BaseCanBusComponent CanBusState CanModelBridge CanModelClient CanObservationJournal ClusterThemeStore
        ICanBusOperator IIpkSyncComponentBase IpkFrames IpkInfoSync IpkLink PhoneDeliveryDiagnostics
        VehicleInputSnapshot VehicleInputStore protocol/dongfeng/DongfengCanBusComponentImpl)
    list(APPEND COMPONENT_SOURCES "${SERVICE}/${NAME}.java")
endforeach()
foreach(PLATFORM qinggan09 qinggan11)
    compile("${WORK}/component-${PLATFORM}" ${COMPONENT_FIXTURES} ${COMPONENT_SOURCES}
        "${ROOT}/src/prepare/test/fixtures/canbus-model/android/net/LocalSocket.java"
        "${ROOT}/src/prepare/test/fixtures/canbus-model/android/net/LocalSocketAddress.java"
        "${ROOT}/src/prepare/test/canbus-observation/com/voyemu/prepare/PhonePeerJsonBridge.java"
        "${ROOT}/src/prepare/java/com/voyemu/prepare/Json.java"
        "${COMPONENT}/${PLATFORM}/com/qinggan/canbus/service/VehicleSignalCatalog.java"
        "${COMPONENT}/qinggan09/com/qinggan/canbus/service/ClusterTheme.java" "${TESTS}/CanBusComponentTest.java")
    run_java("${WORK}/component-${PLATFORM}" com.qinggan.canbus.service.CanBusComponentTest)
endforeach()
file(GLOB_RECURSE CLUSTER_FIXTURES "${ROOT}/src/prepare/test/fixtures/cluster-theme/*.java")
compile("${WORK}/cluster" ${CLUSTER_FIXTURES} "${SERVICE}/ClusterThemeStore.java" "${SERVICE}/ClusterThemeBinder.java"
    "${SERVICE}/ClusterCards.java" "${SERVICE}/IpkSyncBinder.java" "${SERVICE}/CanObservationJournal.java"
    "${SERVICE}/IpkFrames.java" "${SERVICE}/IpkInfoSync.java" "${SERVICE}/IpkLink.java"
    "${COMPONENT}/qinggan11/com/qinggan/canbus/service/ClusterTheme.java" "${TESTS}/ClusterThemeTest.java"
    "${TESTS}/ClusterThemeBinderTest.java" "${TESTS}/ClusterInfoBinderTest.java" "${TESTS}/IpkSyncBinderTest.java"
    "${TESTS}/IpkLinkTest.java" "${ROOT}/src/prepare/test/fixtures/canbus-hfp/android/system/ErrnoException.java"
    "${ROOT}/src/prepare/test/fixtures/canbus-component/android/content/SharedPreferences.java")
run_java("${WORK}/cluster" com.qinggan.canbus.service.ClusterThemeTest)
run_java("${WORK}/cluster" com.qinggan.canbus.service.ClusterInfoBinderTest)
run_java("${WORK}/cluster" com.qinggan.canbus.service.IpkSyncBinderTest)
set(QGAPI "${ROOT}/.local/profiles/free-11-chinese-20260731-7.3.1/originals/framework/QGAPI.jar")
if(TEST_OEM)
if(EXISTS "${QGAPI}")
    run_java("${WORK}/cluster" com.qinggan.canbus.service.ClusterThemeBinderTest)
else()
    message(FATAL_ERROR "ClusterThemeBinderTest requires OEM input: ${QGAPI}")
endif()
endif()
file(GLOB_RECURSE PREPARE "${ROOT}/src/prepare/java/*.java")
compile("${WORK}/host" CLASSPATH "${JAVA_SOURCE_CLASSPATH}" ${PREPARE}
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/CanbusComponentRegression.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/CanbusApiParity.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/CanbusPersistentStartTest.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/ClusterThemeAbiTest.java")
run_java("${WORK}/host${CP_SEPARATOR}${JAVA_SOURCE_CLASSPATH}" com.voyemu.prepare.CanbusComponentRegression
    --manifest-self-test "${ROOT}/src/prepare/test/fixtures/canbus/produced-manifest.txt")
run_java("${WORK}/host${CP_SEPARATOR}${JAVA_SOURCE_CLASSPATH}" com.voyemu.prepare.CanbusPersistentStartTest
    --manifest-self-test "${ROOT}/src/prepare/test/fixtures/canbus/produced-manifest.txt")
if(TEST_OEM)
    run_java("${WORK}/host${CP_SEPARATOR}${JAVA_SOURCE_CLASSPATH}" com.voyemu.prepare.ClusterThemeAbiTest "${QGAPI}")
endif()
# OEM API of the component classes in the APK each profile produces from its imported donor.
if(TEST_OEM)
    foreach(PROFILE IN ITEMS free-09-chinese-20260106-6.0.3 free-11-chinese-20250710-6.11.1 free-11-chinese-20260731-7.3.1)
        run_java("${WORK}/host${CP_SEPARATOR}${JAVA_SOURCE_CLASSPATH}" com.voyemu.prepare.CanbusApiParity
            "${ROOT}" "${WORK}/parity-${PROFILE}" "${PROFILE}" "${ROOT}/src/prepare/test/fixtures/canbus-api/${PROFILE}.txt")
    endforeach()
    # Persistent CanBus package whose Application starts qg.canbus, as the OEM CanBusService of each firmware.
    foreach(PROFILE IN ITEMS free-09-chinese-20221230-nxp-rc45 free-09-chinese-20260106-6.0.3 free-11-chinese-20250710-6.11.1
            free-11-chinese-20260731-7.3.1)
        run_java("${WORK}/host${CP_SEPARATOR}${JAVA_SOURCE_CLASSPATH}" com.voyemu.prepare.CanbusPersistentStartTest
            "${ROOT}" "${WORK}/persistent-${PROFILE}" "${PROFILE}")
    endforeach()
endif()
file(REMOVE_RECURSE "${WORK}")
