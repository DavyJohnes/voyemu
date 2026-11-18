cmake_minimum_required(VERSION 3.29)
# Guest component classes compiled with JVM Android doubles from src/prepare/test and run on the host.
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
include("${ROOT}/src/prepare/test/jdk.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../java_source_dependencies.cmake")
if(TEST_OEM)
    foreach(name framework/QGAPI.jar Launcher.apk)
        if(NOT EXISTS "${ROOT}/.local/profiles/free-11-chinese-20260731-7.3.1/originals/${name}")
            message(FATAL_ERROR "OEM wheel ABI regression requires ${name}")
        endif()
    endforeach()
endif()
set(PLATFORM "${ROOT}/.local/toolchains/java-sdk/platforms/android-34/android.jar")
if(NOT EXISTS "${PLATFORM}")
    message(STATUS "Prepared SDK required: ${PLATFORM}")
    cmake_language(EXIT 77)
endif()
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef TOKEN)
set(WORK "${ROOT}/.local/build/jobs/prepare-java-android-stubs-${TOKEN}")
set(T "${ROOT}/src/prepare/test")
set(A9 "${ROOT}/profiles/free-09-chinese-20260106-6.0.3")
set(F611 "${ROOT}/profiles/free-11-chinese-20250710-6.11.1")
set(F731 "${ROOT}/profiles/free-11-chinese-20260731-7.3.1")
set(PASSION "${ROOT}/profiles/passion-11-russian-20260403-2.1.4.80")
set(WINDOW "${PASSION}/components/window-compat/src/com/voyemu/window")
set(FAILED)
set(PASSED)
# check(CLASS [CLASSPATH ...] [SOURCEPATH ...] [FILES ...] [ARGS ...]); FILES may hold directories.
function(check CLASS)
    cmake_parse_arguments(PARSE_ARGV 1 CASE "" "" "CLASSPATH;SOURCEPATH;FILES;ARGS")
    string(REGEX REPLACE "^.*\\." "" NAME "${CLASS}")
    set(CLASSES "${WORK}/${NAME}")
    file(MAKE_DIRECTORY "${CLASSES}" "${CLASSES}/files" "${CLASSES}/tmp")
    string(REPLACE "." "/" FILE "${CLASS}")
    set(SOURCES "${T}/java/${FILE}.java")
    foreach(ITEM IN LISTS CASE_FILES)
        if(IS_DIRECTORY "${ITEM}")
            file(GLOB_RECURSE FOUND "${ITEM}/*.java")
            list(APPEND SOURCES ${FOUND})
        else()
            list(APPEND SOURCES "${ITEM}")
        endif()
    endforeach()
    set(OPTIONS)
    if(CASE_CLASSPATH)
        list(JOIN CASE_CLASSPATH "${CP_SEPARATOR}" PATHS)
        string(REPLACE ";" "\\;" PATHS "${PATHS}")
        list(APPEND OPTIONS -cp "${PATHS}")
    endif()
    if(CASE_SOURCEPATH)
        list(JOIN CASE_SOURCEPATH "${CP_SEPARATOR}" PATHS)
        string(REPLACE ";" "\\;" PATHS "${PATHS}")
        list(APPEND OPTIONS -sourcepath "${PATHS}" -implicit:class)
    endif()
    execute_process(COMMAND "${JAVAC}" -J-Xmx512m --release 25 -encoding UTF-8 -proc:none -nowarn
        ${OPTIONS} -d "${CLASSES}" ${SOURCES}
        TIMEOUT 120 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    if(RESULT EQUAL 0)
        set(RUNTIME "${CLASSES}" ${CASE_CLASSPATH})
        list(JOIN RUNTIME "${CP_SEPARATOR}" RUNTIME)
        execute_process(COMMAND "${JAVA}" -Xmx256m "-Djava.io.tmpdir=${CLASSES}/tmp" "-Dphone.peer.test.files=${CLASSES}/files" -cp "${RUNTIME}" "${CLASS}" ${CASE_ARGS}
            WORKING_DIRECTORY "${CLASSES}" TIMEOUT 60 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    endif()
    if(RESULT EQUAL 0)
        message(STATUS "${NAME}: passed ${OUTPUT}")
        set(PASSED ${PASSED} "${NAME}" PARENT_SCOPE)
    else()
        message(STATUS "${NAME}: failed ${RESULT}\n${OUTPUT}\n${ERROR}")
        set(FAILED ${FAILED} "${NAME}" PARENT_SCOPE)
    endif()
endfunction()
check(com.voyemu.prepare.PassionAmbientLightTest
    FILES "${T}/fixtures/passion-ambient-light" "${WINDOW}/PassionAmbientLight.java")
check(com.voyemu.prepare.PassionBluetoothVolumeTest
    FILES "${T}/fixtures/passion-bluetooth-volume" "${WINDOW}/PassionBluetoothVolume.java")
check(com.voyemu.prepare.PassionOneKeyMuteTest
    FILES "${T}/fixtures/passion-onekey-mute" "${WINDOW}/PassionOneKeyMute.java")
check(com.voyemu.prepare.SyntheticPhonePeerTest
    FILES "${A9}/components/bluetooth/src/SyntheticPhonePeer.java")
check(com.voyemu.prepare.SyntheticPhonePeerJournalConcurrencyTest
    FILES "${A9}/components/bluetooth/src/SyntheticPhonePeer.java")
check(com.voyemu.prepare.SyntheticPhonePeerJournalConcurrencyTest
    FILES "${ROOT}/profiles/free-09-chinese-20221230-nxp-rc45/components/phone-bluetooth/src/SyntheticPhonePeer.java")
# The OEM-shaped test bases of fixture/oem replace the stub-cls declarations they double.
file(GLOB_RECURSE A9_BLUETOOTH "${A9}/components/bluetooth/src/*.java")
list(FILTER A9_BLUETOOTH EXCLUDE REGEX "/stub-cls/com/qinggan/bluetooth/(profile/(BTAdapter|BluetoothCall|A2dp|BTDevice|SimpleHfpCallback|SimplePbapCallback)|BTDeviceManager|bean/(BTContactItem|BTPhoneNumber))\\.java$")
check(com.voyemu.prepare.SyntheticPhonePeerCallbacksTest
    ARGS "${WORK}/SyntheticPhonePeerCallbacksTest"
    CLASSPATH "${PLATFORM}" SOURCEPATH "${ROOT}/src/prepare/java"
    FILES "${T}/fixtures/phone-peer-process/android" "${T}/fixtures/phone-peer-process/com"
        "${T}/fixtures/phone-peer-process/org" "${T}/fixtures/phone-pbap-race/com"
        "${T}/fixtures/phone-peer-process/fixture/oem" ${A9_BLUETOOTH})
check(com.voyemu.input.FreeKeyInputSessionTest
    SOURCEPATH "${T}/free-startup-audio" "${F731}/components/wheel-input/src")
check(com.voyemu.input.FreeKeySubmissionTest
    SOURCEPATH "${T}/free-startup-audio" "${F731}/components/wheel-input/src")
check(com.voyemu.input.FreeStartupAudioProviderTest
    SOURCEPATH "${T}/free-startup-audio" "${F731}/components/wheel-input/src")
check(com.voyemu.input.FreeWheelBrokerTest
    SOURCEPATH "${T}/free-startup-audio" "${F731}/components/wheel/src")
check(com.voyemu.input.FreeWheelCancellationPolicyTest
    SOURCEPATH "${T}/free-wheel-reflection" "${F731}/components/wheel-platform/src")
check(com.voyemu.input.FreeWheelVolumeTest
    SOURCEPATH "${T}/free-wheel-reflection" "${F731}/components/wheel-platform/src")
check(com.voyemu.input.FreeWheelIngressTest
    SOURCEPATH "${T}/free-wheel-ingress" "${F731}/components/wheel-platform/src" "${F731}/components/wheel-input/src")
check(com.voyemu.input.PhoneControlProviderTest
    SOURCEPATH "${T}/phone-provider" "${A9}/patches/phone-provider/src")
check(com.voyemu.input.PhonePeerProviderTest
    SOURCEPATH "${T}/phone-peer-provider" "${A9}/patches/phone-provider/src")
check(com.voyemu.input.PhonePeerProviderSettingsTest
    SOURCEPATH "${T}/phone-peer-provider" "${T}/phone-provider" "${F611}/patches/phone-provider/src")
check(com.voyemu.input.WheelInputSessionTest
    FILES "${A9}/components/oem-services/src/com/voyemu/input/WheelInputSession.java")
check(com.voyemu.oem.CarSignalStubTest
    FILES "${T}/fixtures/carsignal" "${ROOT}/profiles/common/components/carsignal/src")
check(com.qinggan.audiopolicy.AudioPolicyStubServiceTest
    FILES "${T}/fixtures/audiopolicy-stub" "${A9}/components/audiopolicy/src/compile-only"
        "${A9}/components/audiopolicy/src/com/qinggan/audiopolicy/AudioPolicyStubService.java")
# The qg.power_manager stub of each Free profile with its OEM interface declarations.
foreach(POWER "${A9}/components/oem-services" "${F611}/components/oem-power" "${F731}/components/oem-power")
    check(com.voyemu.oem.PowerManagerStubTest
        FILES "${T}/fixtures/oem-power" "${ROOT}/profiles/common/components/oem-power/src"
            "${POWER}/src/com/voyemu/oem/PowerManagerStub.java"
            "${POWER}/src/compile-only/com/qinggan/power/IPowerManagerListener.java"
            "${POWER}/src/compile-only/com/qinggan/system/IPowerManagerService.java")
endforeach()
check(com.voyemu.output.PassionNavigationReadyTest
    CLASSPATH "${PLATFORM}" SOURCEPATH "${PASSION}/patches/passion-api30-navigation-ready/src")
check(com.voyemu.output.SecondaryDisplayTargetTest SOURCEPATH "${PASSION}/components/bootstrap/src")
# The car signal ABI check reads the imported A9 and A11 donors and A11 clients of the interface.
if(TEST_OEM)
set(CARSIGNAL_ORIGINALS free-09-chinese-20260106-6.0.3/originals/QGIME.apk
    free-11-chinese-20250710-6.11.1/originals/MusicConcate.apk free-11-chinese-20250710-6.11.1/originals/Setting.apk
    free-11-chinese-20260731-7.3.1/originals/MusicConcate.apk free-11-chinese-20260731-7.3.1/originals/framework/QGVehicle.jar)
foreach(ORIGINAL IN LISTS CARSIGNAL_ORIGINALS)
    if(NOT EXISTS "${ROOT}/.local/profiles/${ORIGINAL}")
        message(FATAL_ERROR "CarSignalAbiTest requires OEM original ${ORIGINAL}")
    endif()
endforeach()
check(com.voyemu.oem.CarSignalAbiTest CLASSPATH ${JAVA_LIBRARIES}
    FILES "${T}/fixtures/carsignal" "${ROOT}/profiles/common/components/carsignal/src" ARGS "${ROOT}")
endif()
# The ABI check reads the imported 7.3.1 QGAPI and Launcher originals.
if(TEST_OEM)
if(EXISTS "${ROOT}/.local/profiles/free-11-chinese-20260731-7.3.1/originals/framework/QGAPI.jar"
        AND EXISTS "${ROOT}/.local/profiles/free-11-chinese-20260731-7.3.1/originals/Launcher.apk")
    check(com.voyemu.input.FreeWheelAbiTest CLASSPATH ${JAVA_LIBRARIES} "${PLATFORM}"
        SOURCEPATH "${F731}/components/wheel/src" "${ROOT}/src/prepare/java" ARGS "${ROOT}")
else()
    message(FATAL_ERROR "FreeWheelAbiTest requires OEM originals")
endif()
endif()
if(FAILED)
    message(FATAL_ERROR "Android double failures: ${FAILED}; diagnostics: ${WORK}")
endif()
message(STATUS "Android double checks passed: ${PASSED}")
file(REMOVE_RECURSE "${WORK}")
