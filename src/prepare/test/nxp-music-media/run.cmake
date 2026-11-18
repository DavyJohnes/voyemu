cmake_minimum_required(VERSION 3.29)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include("${ROOT}/src/prepare/test/jdk.cmake")
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef TOKEN)
set(WORK "${ROOT}/.local/build/jobs/nxp-music-media-test-${TOKEN}")
file(MAKE_DIRECTORY "${WORK}/behavior")
file(GLOB_RECURSE STUBS "${CMAKE_CURRENT_LIST_DIR}/android/*.java")
file(GLOB_RECURSE OEM_STUBS "${CMAKE_CURRENT_LIST_DIR}/oem/*.java")
execute_process(COMMAND "${JAVAC}" -J-Xmx128m --release 8 -d "${WORK}/behavior" ${STUBS}
    "${CMAKE_CURRENT_LIST_DIR}/NxpMusicPcmTest.java"
    "${ROOT}/profiles/free-09-chinese-20221230-nxp-rc45/patches/nxp-music-media-compat/src/com/voyemu/output/NxpMusicPcm.java"
    ${OEM_STUBS} "${CMAKE_CURRENT_LIST_DIR}/NxpMusicStorageTest.java"
    "${ROOT}/profiles/free-09-chinese-20221230-nxp-rc45/patches/nxp-music-media-compat/src/com/voyemu/output/NxpMusicStorage.java"
    "${CMAKE_CURRENT_LIST_DIR}/NxpMusicMediaTest.java"
    "${ROOT}/profiles/free-09-chinese-20221230-nxp-rc45/patches/nxp-music-media-compat/src/com/voyemu/output/NxpMusicMedia.java"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
execute_process(COMMAND "${JAVA}" -Xmx128m -cp "${WORK}/behavior" NxpMusicMediaTest
    TIMEOUT 15 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
message(STATUS "${OUTPUT}")
execute_process(COMMAND "${JAVA}" -Xmx128m "-Djava.io.tmpdir=${WORK}" -cp "${WORK}/behavior" NxpMusicPcmTest
    TIMEOUT 15 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
message(STATUS "${OUTPUT}")
execute_process(COMMAND "${JAVA}" -Xmx128m -cp "${WORK}/behavior" NxpMusicStorageTest
    TIMEOUT 15 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
message(STATUS "${OUTPUT}")
include("${CMAKE_CURRENT_LIST_DIR}/../java_source_dependencies.cmake")
file(MAKE_DIRECTORY "${WORK}/production")
execute_process(COMMAND "${JAVAC}" -J-Xmx512m --release 25 -cp "${JAVA_SOURCE_CLASSPATH}"
    -sourcepath "${ROOT}/src/prepare/java" -d "${WORK}/production"
    "${ROOT}/src/prepare/java/com/voyemu/prepare/Recipes.java"
    "${ROOT}/src/prepare/test/java/com/voyemu/prepare/NxpMusicMediaMetadataTest.java"
    TIMEOUT 60 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
execute_process(COMMAND "${JAVA}" -Xmx512m -cp "${WORK}/production${CP_SEPARATOR}${JAVA_SOURCE_CLASSPATH}"
    com.voyemu.prepare.NxpMusicMediaMetadataTest "${ROOT}" "${WORK}/recipe"
    TIMEOUT 30 RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR)
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUTPUT}${ERROR}")
endif()
message(STATUS "${OUTPUT}")
file(REMOVE_RECURSE "${WORK}")
