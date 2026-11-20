cmake_minimum_required(VERSION 3.29)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${ROOT}/src/prepare/test/jdk.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../java_source_dependencies.cmake")
if(TEST_OEM)
    foreach(name CarSettings.apk LY_Multimedia.apk LY_Camera.apk)
        if(NOT EXISTS "${ROOT}/.local/profiles/passion-11-russian-20260403-2.1.4.80/originals/${name}")
            message(FATAL_ERROR "OEM Passion regression requires ${name}")
        endif()
    endforeach()
endif()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef TOKEN)
set(WORK "${ROOT}/.local/build/jobs/passion-carsettings-test-${TOKEN}")
file(MAKE_DIRECTORY "${WORK}")
file(GLOB_RECURSE SOUND_STUBS "${CMAKE_CURRENT_LIST_DIR}/stubs/*.java")
execute_process(COMMAND "${JAVAC}" --release 8 -d "${WORK}/sound-test" ${SOUND_STUBS}
    "${ROOT}/profiles/passion-11-russian-20260403-2.1.4.80/patches/passion-api30-settings-sound/src/com/voyemu/settings/PassionSoundStreams.java"
    "${CMAKE_CURRENT_LIST_DIR}/PassionSoundStreamsTest.java"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
execute_process(COMMAND "${JAVA}" -cp "${WORK}/sound-test" PassionSoundStreamsTest
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
message(STATUS "${OUTPUT}")
file(GLOB_RECURSE WHEEL_STUBS "${ROOT}/src/prepare/test/passion-wheel/stubs/*.java")
set(WHEEL_SOURCE "${ROOT}/profiles/passion-11-russian-20260403-2.1.4.80/components/wheel-input/src/com/voyemu/input")
execute_process(COMMAND "${JAVAC}" --release 25 -d "${WORK}/wheel-test" ${WHEEL_STUBS}
    "${WHEEL_SOURCE}/PassionWheelSession.java" "${WHEEL_SOURCE}/PassionWheelTerminal.java" "${WHEEL_SOURCE}/PassionStartupAudioPolicy.java"
    "${ROOT}/src/prepare/test/passion-wheel/PassionWheelSessionTest.java"
    "${ROOT}/src/prepare/test/passion-wheel/TerminalTest.java"
    "${ROOT}/src/prepare/test/passion-wheel/StartupAudioTest.java"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
foreach(TEST com.voyemu.prepare.PassionWheelSessionTest com.voyemu.input.TerminalTest com.voyemu.input.StartupAudioTest)
    execute_process(COMMAND "${JAVA}" -cp "${WORK}/wheel-test" "${TEST}"
        TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "${OUTPUT}${ERROR}")
    endif()
    message(STATUS "${OUTPUT}")
endforeach()
# Reuse provider doubles while retaining the wheel-specific Android audio/key API.
file(GLOB_RECURSE WHEEL_PROVIDER_STUBS "${ROOT}/src/prepare/test/free-startup-audio/android/*.java"
    "${ROOT}/src/prepare/test/free-startup-audio/org/*.java")
list(FILTER WHEEL_PROVIDER_STUBS EXCLUDE REGEX "/android/media/AudioManager.java$")
execute_process(COMMAND "${JAVAC}" --release 25 -d "${WORK}/wheel-provider-test" ${WHEEL_PROVIDER_STUBS}
    "${ROOT}/src/prepare/test/passion-wheel/stubs/android/media/AudioManager.java"
    "${ROOT}/src/prepare/test/passion-wheel/stubs/android/view/KeyEvent.java"
    "${WHEEL_SOURCE}/PassionWheelProvider.java" "${WHEEL_SOURCE}/PassionWheelSession.java"
    "${WHEEL_SOURCE}/PassionWheelTerminal.java" "${WHEEL_SOURCE}/PassionStartupAudioPolicy.java"
    "${ROOT}/src/prepare/test/passion-wheel/PassionWheelProviderTest.java"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
execute_process(COMMAND "${JAVA}" -cp "${WORK}/wheel-provider-test" com.voyemu.input.PassionWheelProviderTest
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
message(STATUS "${OUTPUT}")
execute_process(COMMAND "${JAVAC}" -J-Xmx768m --release 25 -cp "${JAVA_SOURCE_CLASSPATH}"
    -sourcepath "${ROOT}/src/prepare/java${CP_SEPARATOR}${ROOT}/src/prepare/test/java" -d "${WORK}"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/PassionGeneratedHelperContractTest.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/SigningIntermediateTest.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/PassionCarSettingsRecyclerRecipeTest.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/PassionCarSettingsRecyclerGuardTest.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/PassionSettingsBluetoothRoleGuardTest.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/PassionSettingsSoundGuardTest.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/PassionSettingsUnitsGuardTest.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/PassionSettingsUnitsRecipeTest.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/PassionCameraVirtualGuardTest.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/PassionCameraVirtualRecipeTest.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/PassionSettingsAudioBluetoothRecipeTest.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/PassionUsbMediaSessionGuardTest.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/PassionMusicInsetsGuardTest.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/PassionUsbMediaSessionRecipeTest.java"
    TIMEOUT 60 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
set(CLASSPATH "${WORK}${CP_SEPARATOR}${JAVA_SOURCE_CLASSPATH}")
execute_process(COMMAND "${JAVA}" -Xmx512m -cp "${CLASSPATH}"
    com.voyemu.prepare.PassionCarSettingsRecyclerRecipeTest "${ROOT}"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
message(STATUS "${OUTPUT}")
set(OEM "${ROOT}/.local/profiles/passion-11-russian-20260403-2.1.4.80/originals/CarSettings.apk")
set(PLATFORM "${ROOT}/.local/toolchains/java-sdk/platforms/android-34/android.jar")
if(TEST_OEM)
if(EXISTS "${OEM}")
    execute_process(COMMAND "${JAVA}" -Xmx512m -cp "${CLASSPATH}"
        com.voyemu.prepare.PassionCarSettingsRecyclerGuardTest "${OEM}"
        TIMEOUT 60 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "${OUTPUT}${ERROR}")
    endif()
    message(STATUS "${OUTPUT}")
    foreach(TEST PassionSettingsBluetoothRoleGuardTest PassionSettingsSoundGuardTest PassionSettingsUnitsGuardTest)
        execute_process(COMMAND "${JAVA}" -Xmx512m -cp "${CLASSPATH}" "com.voyemu.prepare.${TEST}" "${OEM}"
            TIMEOUT 60 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
        if(NOT RESULT EQUAL 0)
            message(FATAL_ERROR "${OUTPUT}${ERROR}")
        endif()
        message(STATUS "${OUTPUT}")
    endforeach()
    set(PLATFORM "${ROOT}/.local/toolchains/java-sdk/platforms/android-34/android.jar")
    execute_process(COMMAND "${JAVA}" -Xmx512m -cp "${CLASSPATH}"
        com.voyemu.prepare.PassionSettingsAudioBluetoothRecipeTest "${ROOT}" "${WORK}" "${OEM}" "${PLATFORM}"
        TIMEOUT 60 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "${OUTPUT}${ERROR}")
    endif()
    message(STATUS "${OUTPUT}")
else()
    message(FATAL_ERROR "OEM CarSettings required for registered regression")
endif()
endif()
file(GLOB_RECURSE USB_MEDIA_STUBS "${ROOT}/src/prepare/test/passion-usb-media/stubs/*.java")
execute_process(COMMAND "${JAVAC}" --release 8 -d "${WORK}/usb-media-test" ${USB_MEDIA_STUBS}
    "${ROOT}/profiles/passion-11-russian-20260403-2.1.4.80/patches/passion-api30-usb-media-session/src/com/voyemu/media/PassionUsbMediaSession.java"
    "${ROOT}/src/prepare/test/passion-usb-media/PassionUsbMediaSessionTest.java"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
execute_process(COMMAND "${JAVA}" -cp "${WORK}/usb-media-test" PassionUsbMediaSessionTest
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
message(STATUS "${OUTPUT}")
set(USB_OEM "${ROOT}/.local/profiles/passion-11-russian-20260403-2.1.4.80/originals/LY_Multimedia.apk")
if(TEST_OEM)
if(EXISTS "${USB_OEM}")
    execute_process(COMMAND "${JAVA}" -Xmx512m -cp "${CLASSPATH}"
        com.voyemu.prepare.PassionMusicInsetsGuardTest "${ROOT}" "${WORK}/music-insets"
        TIMEOUT 180 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "${OUTPUT}${ERROR}")
    endif()
    message(STATUS "${OUTPUT}")
    execute_process(COMMAND "${JAVA}" -Xmx512m -cp "${CLASSPATH}"
        com.voyemu.prepare.PassionUsbMediaSessionGuardTest "${USB_OEM}"
        "${ROOT}/profiles/passion-11-russian-20260403-2.1.4.80/patches/passion-api30-usb-media-session/src"
        "${ROOT}/.local/toolchains/java-sdk/platforms/android-34/android.jar" "${WORK}/usb-helper"
        TIMEOUT 180 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "${OUTPUT}${ERROR}")
    endif()
    message(STATUS "${OUTPUT}")
else()
    message(FATAL_ERROR "OEM USB media required for registered regression")
endif()
endif()
execute_process(COMMAND "${JAVA}" -cp "${CLASSPATH}" com.voyemu.prepare.PassionUsbMediaSessionRecipeTest "${ROOT}"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
message(STATUS "${OUTPUT}")

execute_process(COMMAND "${JAVA}" -cp "${CLASSPATH}" com.voyemu.prepare.PassionSettingsUnitsRecipeTest "${ROOT}"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
message(STATUS "${OUTPUT}")

set(CAMERA_OEM "${ROOT}/.local/profiles/passion-11-russian-20260403-2.1.4.80/originals/LY_Camera.apk")
if(TEST_OEM)
if(EXISTS "${CAMERA_OEM}")
    execute_process(COMMAND "${JAVA}" -Xmx512m -cp "${CLASSPATH}" com.voyemu.prepare.PassionCameraVirtualGuardTest "${CAMERA_OEM}"
        TIMEOUT 180 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "${OUTPUT}${ERROR}")
    endif()
    message(STATUS "${OUTPUT}")
else()
    message(FATAL_ERROR "OEM Camera required for registered regression")
endif()
endif()
execute_process(COMMAND "${JAVA}" -cp "${CLASSPATH}" com.voyemu.prepare.PassionCameraVirtualRecipeTest "${ROOT}"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
message(STATUS "${OUTPUT}")
if(TEST_OEM)
execute_process(COMMAND "${JAVA}" -Xmx512m -cp "${CLASSPATH}"
    com.voyemu.prepare.PassionGeneratedHelperContractTest "${ROOT}" "${PLATFORM}" "${WORK}/generated-helper-contract"
    TIMEOUT 180 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
message(STATUS "${OUTPUT}")
endif()
