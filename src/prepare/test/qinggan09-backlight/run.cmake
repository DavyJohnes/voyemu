cmake_minimum_required(VERSION 3.29)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${ROOT}/src/prepare/test/jdk.cmake")
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef TOKEN)
set(WORK "${ROOT}/.local/build/jobs/backlight-jni-test-${TOKEN}")
set(SOURCE "${ROOT}/profiles/common/components/qg-runtime/src/qg_runtime.c")
file(MAKE_DIRECTORY "${WORK}/classes")
if(HOST_SYSTEM STREQUAL "Darwin")
    set(JNI_HOST darwin)
    set(SHARED -dynamiclib)
else()
    set(JNI_HOST linux)
    set(SHARED -shared)
endif()
set(FLAGS)
if(COMPILER_TARGET)
    list(APPEND FLAGS "--target=${COMPILER_TARGET}")
endif()
if(SYSROOT)
    list(APPEND FLAGS -isysroot "${SYSROOT}" "-L${SYSROOT}/usr/lib" "-F${SYSROOT}/System/Library/Frameworks")
endif()
if(ZIG_LIBC)
    set(ENV{ZIG_LIBC} "${ZIG_LIBC}")
endif()
execute_process(COMMAND "${CC}" ${FLAGS} ${SHARED} -fPIC -O2 -g0 -std=c11
    "-I$ENV{JAVA_HOME}/include" "-I$ENV{JAVA_HOME}/include/${JNI_HOST}" "-I${CMAKE_CURRENT_LIST_DIR}"
    -include "${CMAKE_CURRENT_LIST_DIR}/test-path.h" "${SOURCE}" -o "${WORK}/libqg-runtime-test.so"
    TIMEOUT 90 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Backlight native fixture compile: ${RESULT}\n${OUTPUT}\n${ERROR}")
endif()
execute_process(COMMAND "${JAVAC}" -J-Xmx128m --release 25 -d "${WORK}/classes"
    "${CMAKE_CURRENT_LIST_DIR}/src/compile-only/com/qinggan/os/BackLightNative.java"
    "${CMAKE_CURRENT_LIST_DIR}/src/compile-only/com/qinggan/os/NvramNative.java"
    "${CMAKE_CURRENT_LIST_DIR}/com/qinggan/os/BackLightNativeTest.java"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Backlight JNI test compile: ${RESULT}\n${OUTPUT}\n${ERROR}")
endif()
# The store starts absent: the first callers copy the read-only seed.
file(WRITE "${WORK}/seed" "43=equipment\n51=original\nforeign.record=keep\n")
file(MAKE_DIRECTORY "${WORK}/data")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VOYEMU_TEST_NVRAM=${WORK}/data/nvram"
    "VOYEMU_TEST_NVRAM_SEED=${WORK}/seed"
    "${JAVA}" -Xmx128m -cp "${WORK}/classes" com.qinggan.os.BackLightNativeTest "${WORK}/libqg-runtime-test.so"
    TIMEOUT 60 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "Backlight JNI regression: ${RESULT}\n${OUTPUT}\n${ERROR}")
endif()
file(REMOVE_RECURSE "${WORK}")
message(STATUS "${OUTPUT}")
