# Builds the e2fsprogs image helpers a9-offline-labels and api30-root-layout of one host from the pinned
# e2fsprogs release tarball and the helper sources with the pinned Zig: the recipe of the release assets
# that preparation downloads by the e2fsprogs.helpers pins of src/app/toolchain.json. The build directory
# has a random name and the file prefix map removes it, so every build of one target yields the same
# bytes on any host with a POSIX shell and make. The e2fs-helpers workflow runs it on Linux.
# Usage: cmake -DVOYEMU_E2FS_TARGET=linux-x64|darwin-arm64|win32-x64 -DVOYEMU_E2FS_OUTPUT=<directory>
#              -P src/app/prepare/e2fsprogs_build.cmake
cmake_minimum_required(VERSION 3.29)
get_filename_component(voyemu_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT VOYEMU_E2FS_TARGET OR NOT VOYEMU_E2FS_OUTPUT)
    message(FATAL_ERROR "VOYEMU_E2FS_TARGET and VOYEMU_E2FS_OUTPUT are required")
endif()
if(VOYEMU_E2FS_TARGET STREQUAL linux-x64)
    # The glibc baseline of the launcher: the toolchain file only sets variables.
    include("${voyemu_root}/src/dev/toolchains/zig-linux-x64.cmake")
    set(zig_target ${CMAKE_C_COMPILER_TARGET})
    set(configure_host x86_64-linux-gnu)
elseif(VOYEMU_E2FS_TARGET STREQUAL darwin-arm64)
    # Zig's bundled macOS headers and libSystem stubs: the helpers do not depend on the build host.
    set(zig_target aarch64-macos.13.0)
    set(configure_host aarch64-apple-darwin)
elseif(VOYEMU_E2FS_TARGET STREQUAL win32-x64)
    # MinGW: e2fsprogs selects its Windows I/O manager and the include/mingw compatibility headers.
    set(zig_target x86_64-windows-gnu)
    set(configure_host x86_64-w64-mingw32)
    set(compat_include include/mingw)
else()
    message(FATAL_ERROR "Unknown e2fsprogs helper target ${VOYEMU_E2FS_TARGET}; expected linux-x64, darwin-arm64 or win32-x64")
endif()
file(READ "${voyemu_root}/src/app/toolchain.json" pins)
foreach(key version url sha256)
    string(JSON e2fsprogs_${key} GET "${pins}" e2fsprogs ${key})
endforeach()
set(archive "${voyemu_root}/.local/toolchains/downloads/sources/e2fsprogs-${e2fsprogs_version}.tar.gz")
if(NOT EXISTS "${archive}")
    file(DOWNLOAD "${e2fsprogs_url}" "${archive}" EXPECTED_HASH SHA256=${e2fsprogs_sha256} STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        file(REMOVE "${archive}")
        message(FATAL_ERROR "e2fsprogs download failed: ${status}")
    endif()
endif()
file(SHA256 "${archive}" actual)
if(NOT actual STREQUAL e2fsprogs_sha256)
    message(FATAL_ERROR "e2fsprogs archive SHA-256 ${actual} differs from the pin")
endif()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef token)
set(work "${voyemu_root}/.local/build/jobs/e2fsprogs-${token}")
file(MAKE_DIRECTORY "${work}/build" "${work}/pkg-config")
file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${work}")
set(source "${work}/e2fsprogs-${e2fsprogs_version}")
set(toolchains "${voyemu_root}/src/dev/toolchains")
set(flags -O2 -g0 "-ffile-prefix-map=${work}=.")
list(JOIN flags " " cflags)
# A private Zig cache keeps concurrent builds apart; BUILD_CC compiles the generators that run on the build host.
set(environment
    LC_ALL=C
    "CPPFLAGS="
    "LDFLAGS="
    "PKG_CONFIG_PATH="
    "PKG_CONFIG_LIBDIR=${work}/pkg-config"
    "ZIG_GLOBAL_CACHE_DIR=${work}/zig-cache"
    "ZIG_LOCAL_CACHE_DIR=${work}/zig-cache"
    "CC=${toolchains}/zig-cc -target ${zig_target}"
    "BUILD_CC=${toolchains}/zig-cc"
    "AR=${toolchains}/zig-ar"
    "RANLIB=${toolchains}/zig-ranlib"
    "CFLAGS=${cflags}")
# Only libcom_err and libext2fs are built; disable discovery of optional host libraries.
# The generated libext2fs headers come first because parallel make does not order them before every object.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${environment}
                        sh "${source}/configure" --host=${configure_host} --disable-nls --disable-elf-shlibs
                        --disable-bsd-shlibs --disable-fuse2fs --without-libarchive
                WORKING_DIRECTORY "${work}/build" OUTPUT_FILE "${work}/configure.log" ERROR_FILE "${work}/configure.log"
                RESULT_VARIABLE result)
foreach(step "lib/dirpaths.h" "-C;lib/et" "-C;lib/ext2fs;ext2_err.h;crc32c_table.h" "-C;lib/ext2fs")
    if(result EQUAL 0)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${environment} make -j2 ${step}
                        WORKING_DIRECTORY "${work}/build" OUTPUT_FILE "${work}/make.log" ERROR_FILE "${work}/make.log"
                        RESULT_VARIABLE result)
    endif()
endforeach()
file(MAKE_DIRECTORY "${VOYEMU_E2FS_OUTPUT}")
set(compat_flags)
if(compat_include)
    # As in the library build, the compatibility linux/types.h defines the integer types and dev_t first.
    set(compat_flags "-I${source}/${compat_include}" -include linux/types.h)
endif()
foreach(helper a9-offline-labels api30-root-layout)
    string(REPLACE "-" "_" name "${helper}")
    if(result EQUAL 0)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${environment}
                                "${toolchains}/zig-cc" -target ${zig_target} ${flags} -s -Wall -Werror
                                "-I${work}/build/lib" "-I${source}/lib"
                                ${compat_flags}
                                "${voyemu_root}/src/app/prepare/${name}_helper.c"
                                "${work}/build/lib/libext2fs.a" "${work}/build/lib/libcom_err.a"
                                -o "${work}/${helper}"
                        RESULT_VARIABLE result)
        if(result EQUAL 0)
            file(COPY_FILE "${work}/${helper}" "${VOYEMU_E2FS_OUTPUT}/${helper}-${VOYEMU_E2FS_TARGET}")
            file(SHA256 "${work}/${helper}" built)
            message(STATUS "Built ${helper} for ${VOYEMU_E2FS_TARGET}: ${built}")
        endif()
    endif()
endforeach()
if(NOT result EQUAL 0)
    message(FATAL_ERROR "e2fsprogs helper build for ${VOYEMU_E2FS_TARGET} failed; logs in ${work}")
endif()
file(COPY_FILE "${source}/NOTICE" "${VOYEMU_E2FS_OUTPUT}/NOTICE")
file(REMOVE_RECURSE "${work}")
