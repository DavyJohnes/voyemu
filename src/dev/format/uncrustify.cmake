# Installs the pinned Uncrustify for this host into optionalTools.uncrustify.directory. By default it
# downloads the prebuilt binary optionalTools.uncrustify.binaries.<host>, which the uncrustify workflow
# builds and publishes, and checks its SHA-256. VOYEMU_UNCRUSTIFY_FROM_SOURCE=ON builds the binary from
# the pinned release tarball with the pinned Zig instead and checks it against the same pin.
# VOYEMU_UNCRUSTIFY_TARGET with VOYEMU_UNCRUSTIFY_OUTPUT builds the binary of any host into a file
# without the pin check: the recipe the workflow runs. The upstream build needs Python 3 to generate
# its option tables; only the source build needs it.
# Usage: cmake [-DVOYEMU_UNCRUSTIFY_FROM_SOURCE=ON] -P src/dev/format/uncrustify.cmake
#        cmake -DVOYEMU_UNCRUSTIFY_TARGET=linux-x64|darwin-arm64|win32-x64
#              -DVOYEMU_UNCRUSTIFY_OUTPUT=<file> -P src/dev/format/uncrustify.cmake
cmake_minimum_required(VERSION 3.29)
get_filename_component(voyemu_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
file(READ "${voyemu_root}/src/app/toolchain.json" pins)
foreach(key version url sha256 directory binary)
    string(JSON uncrustify_${key} GET "${pins}" optionalTools uncrustify ${key})
endforeach()
set(toolchains "${voyemu_root}/src/dev/toolchains")
if(CMAKE_HOST_WIN32)
    set(host win32-x64)
elseif(CMAKE_HOST_APPLE)
    set(host darwin-arm64)
else()
    set(host linux-x64)
endif()

# Builds the binary for target from the pinned tarball into output. The build directory has a random
# name and the file prefix map removes it, so every build of one target yields the same bytes.
function(voyemu_uncrustify_build target output)
    if(target STREQUAL linux-x64)
        set(toolchain_args "-DCMAKE_TOOLCHAIN_FILE=${toolchains}/zig-linux-x64.cmake")
    elseif(target STREQUAL win32-x64)
        set(toolchain_args "-DCMAKE_TOOLCHAIN_FILE=${toolchains}/zig-windows-x64.cmake")
    elseif(target STREQUAL darwin-arm64)
        # Zig's bundled macOS headers and libSystem stubs instead of zig-macos-arm64.cmake, which needs
        # the SDK of a macOS host: the binary must not depend on the build host. The empty sysroot and
        # deployment target keep CMake from adding SDK flags on a macOS host.
        set(toolchain_args
            -DCMAKE_SYSTEM_NAME=Darwin
            -DCMAKE_SYSTEM_PROCESSOR=arm64
            "-DCMAKE_C_COMPILER=${toolchains}/zig-cc"
            "-DCMAKE_CXX_COMPILER=${toolchains}/zig-c++"
            -DCMAKE_C_COMPILER_TARGET=aarch64-macos.13.0
            -DCMAKE_CXX_COMPILER_TARGET=aarch64-macos.13.0
            "-DCMAKE_AR=${toolchains}/zig-ar"
            "-DCMAKE_RANLIB=${toolchains}/zig-ranlib"
            -DCMAKE_OSX_SYSROOT=
            -DCMAKE_OSX_DEPLOYMENT_TARGET=
            -DCMAKE_C_FLAGS_INIT=-g0
            -DCMAKE_CXX_FLAGS_INIT=-g0
            -DCMAKE_EXE_LINKER_FLAGS_INIT=-s)
    else()
        message(FATAL_ERROR "Unknown Uncrustify target ${target}; expected linux-x64, darwin-arm64 or win32-x64")
    endif()
    set(archive "${voyemu_root}/.local/toolchains/downloads/sources/uncrustify-${uncrustify_version}.tar.gz")
    if(NOT EXISTS "${archive}")
        file(DOWNLOAD "${uncrustify_url}" "${archive}" EXPECTED_HASH SHA256=${uncrustify_sha256} STATUS status)
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            file(REMOVE "${archive}")
            message(FATAL_ERROR "Uncrustify download failed: ${status}")
        endif()
    endif()
    file(SHA256 "${archive}" actual)
    if(NOT actual STREQUAL uncrustify_sha256)
        message(FATAL_ERROR "Uncrustify archive SHA-256 ${actual} differs from the pin")
    endif()
    string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef token)
    set(work "${voyemu_root}/.local/build/jobs/uncrustify-${token}")
    file(MAKE_DIRECTORY "${work}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${work}")
    file(GLOB source LIST_DIRECTORIES true "${work}/uncrustify-*")
    # CMake joins CFLAGS and CXXFLAGS with the toolchain flags.
    set(ENV{CFLAGS} "-ffile-prefix-map=${work}=.")
    set(ENV{CXXFLAGS} "-ffile-prefix-map=${work}=.")
    # NoGitVersionString keeps the enclosing checkout out of the version string.
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${work}/build" -G Ninja
                            -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DNoGitVersionString=ON
                            ${toolchain_args}
                    RESULT_VARIABLE result)
    if(result EQUAL 0)
        execute_process(COMMAND "${CMAKE_COMMAND}" --build "${work}/build" --target uncrustify
                        RESULT_VARIABLE result)
    endif()
    if(NOT result EQUAL 0)
        file(REMOVE_RECURSE "${work}")
        message(FATAL_ERROR "Uncrustify build for ${target} failed")
    endif()
    set(name uncrustify)
    if(target STREQUAL win32-x64)
        set(name uncrustify.exe)
    endif()
    get_filename_component(output_directory "${output}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    file(COPY_FILE "${work}/build/${name}" "${output}")
    file(REMOVE_RECURSE "${work}")
endfunction()

if(VOYEMU_UNCRUSTIFY_TARGET)
    if(NOT VOYEMU_UNCRUSTIFY_OUTPUT)
        message(FATAL_ERROR "VOYEMU_UNCRUSTIFY_TARGET requires VOYEMU_UNCRUSTIFY_OUTPUT")
    endif()
    voyemu_uncrustify_build(${VOYEMU_UNCRUSTIFY_TARGET} "${VOYEMU_UNCRUSTIFY_OUTPUT}")
    file(SHA256 "${VOYEMU_UNCRUSTIFY_OUTPUT}" built)
    message(STATUS "Built Uncrustify ${uncrustify_version} for ${VOYEMU_UNCRUSTIFY_TARGET}: ${built}")
    return()
endif()

string(JSON binary_url GET "${pins}" optionalTools uncrustify binaries ${host} url)
string(JSON binary_sha256 GET "${pins}" optionalTools uncrustify binaries ${host} sha256)
set(binary "${voyemu_root}/${uncrustify_directory}/${uncrustify_binary}")
if(CMAKE_HOST_WIN32)
    string(APPEND binary ".exe")
endif()
if(EXISTS "${binary}")
    file(SHA256 "${binary}" actual)
    if(actual STREQUAL binary_sha256)
        message(STATUS "Uncrustify ${uncrustify_version} already installed")
        return()
    endif()
endif()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef token)
set(candidate "${voyemu_root}/.local/build/jobs/uncrustify-${token}.bin")
if(VOYEMU_UNCRUSTIFY_FROM_SOURCE)
    voyemu_uncrustify_build(${host} "${candidate}")
else()
    file(DOWNLOAD "${binary_url}" "${candidate}" STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        file(REMOVE "${candidate}")
        message(FATAL_ERROR "Uncrustify download failed: ${status}; -DVOYEMU_UNCRUSTIFY_FROM_SOURCE=ON builds it from the pinned source")
    endif()
endif()
file(SHA256 "${candidate}" actual)
if(NOT actual STREQUAL binary_sha256)
    file(REMOVE "${candidate}")
    message(FATAL_ERROR "Uncrustify binary SHA-256 ${actual} differs from the ${host} pin ${binary_sha256}")
endif()
get_filename_component(target_directory "${binary}" DIRECTORY)
file(MAKE_DIRECTORY "${target_directory}")
file(COPY_FILE "${candidate}" "${binary}")
file(REMOVE "${candidate}")
file(CHMOD "${binary}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
message(STATUS "Installed Uncrustify ${uncrustify_version} at ${binary}")
