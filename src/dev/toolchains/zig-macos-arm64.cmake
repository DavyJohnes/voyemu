# Native macOS arm64 toolchain on the pinned Zig 0.15.2 with the Command Line Tools SDK; Xcode is not required.
# Use with -G Ninja -DCMAKE_TOOLCHAIN_FILE="$PWD/src/dev/toolchains/zig-macos-arm64.cmake" from the project root.
if(NOT VOYEMU_MACOS_SDK)
    execute_process(COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE voyemu_macos_sdk OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE voyemu_xcrun_result)
    if(NOT voyemu_xcrun_result EQUAL 0)
        message(FATAL_ERROR "macOS SDK not found; install the Command Line Tools (xcode-select --install) or pass -DVOYEMU_MACOS_SDK=<MacOSX.sdk>")
    endif()
    set(VOYEMU_MACOS_SDK "${voyemu_macos_sdk}" CACHE PATH "macOS SDK used as the Zig sysroot")
endif()
if(NOT EXISTS "${VOYEMU_MACOS_SDK}/usr/include/stdio.h" OR NOT IS_DIRECTORY "${VOYEMU_MACOS_SDK}/System/Library/Frameworks")
    message(FATAL_ERROR "Not a macOS SDK: ${VOYEMU_MACOS_SDK}")
endif()
# An explicit target selects Zig's bundled headers; a libc file in ZIG_LIBC switches compilation to the SDK
# headers and frameworks. One file per build tree is shared with try_compile projects.
if(NOT VOYEMU_ZIG_LIBC)
    set(VOYEMU_ZIG_LIBC "${CMAKE_BINARY_DIR}/zig-macos-libc.txt")
endif()
file(CONFIGURE OUTPUT "${VOYEMU_ZIG_LIBC}" CONTENT "include_dir=${VOYEMU_MACOS_SDK}/usr/include
sys_include_dir=${VOYEMU_MACOS_SDK}/usr/include
crt_dir=
msvc_lib_dir=
kernel32_lib_dir=
gcc_dir=
")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES VOYEMU_MACOS_SDK VOYEMU_ZIG_LIBC)
set(ENV{ZIG_LIBC} "${VOYEMU_ZIG_LIBC}")
set(voyemu_zig_env "${CMAKE_COMMAND};-E;env;ZIG_LIBC=${VOYEMU_ZIG_LIBC}")
set(CMAKE_C_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-cc")
set(CMAKE_CXX_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-c++")
set(CMAKE_OBJC_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-cc")
set(CMAKE_OBJCXX_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-c++")
foreach(lang C CXX OBJC OBJCXX)
    # The minimum macOS version is Zig's aarch64-macos default, stated explicitly.
    set(CMAKE_${lang}_COMPILER_TARGET aarch64-macos.13.0)
    set(CMAKE_${lang}_COMPILER_LAUNCHER "${voyemu_zig_env}")
    set(CMAKE_${lang}_LINKER_LAUNCHER "${voyemu_zig_env}")
endforeach()
set(CMAKE_AR "${CMAKE_CURRENT_LIST_DIR}/zig-ar")
set(CMAKE_RANLIB "${CMAKE_CURRENT_LIST_DIR}/zig-ranlib")
set(CMAKE_OSX_SYSROOT "${VOYEMU_MACOS_SDK}")
set(CMAKE_OSX_DEPLOYMENT_TARGET "")
# The Zig linker searches neither SDK libraries nor frameworks on its own. Zig signs arm64 output ad hoc.
set(voyemu_macos_sdk_link "-L${VOYEMU_MACOS_SDK}/usr/lib -F${VOYEMU_MACOS_SDK}/System/Library/Frameworks")
# Zig emits debug info by default; Release builds carry none.
set(CMAKE_C_FLAGS_INIT -g0)
set(CMAKE_CXX_FLAGS_INIT -g0)
set(CMAKE_OBJC_FLAGS_INIT -g0)
set(CMAKE_OBJCXX_FLAGS_INIT -g0)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-s ${voyemu_macos_sdk_link}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-s ${voyemu_macos_sdk_link}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "-s ${voyemu_macos_sdk_link}")
