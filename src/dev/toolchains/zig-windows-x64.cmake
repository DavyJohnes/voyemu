# Toolchain on the pinned Zig 0.15.2 for target x86_64-windows-gnu: native on Windows, cross on Linux.
# Use with -G Ninja -DCMAKE_TOOLCHAIN_FILE="$PWD/src/dev/toolchains/zig-windows-x64.cmake" from the project root (sh or PowerShell).
if(CMAKE_HOST_WIN32)
    # Windows hosts run the .cmd forms of the sh wrappers.
    set(voyemu_zig_wrapper_suffix .cmd)
else()
    set(CMAKE_SYSTEM_NAME Windows)
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
    set(voyemu_zig_wrapper_suffix "")
endif()
set(CMAKE_C_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-cc${voyemu_zig_wrapper_suffix}")
set(CMAKE_CXX_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-c++${voyemu_zig_wrapper_suffix}")
set(CMAKE_C_COMPILER_TARGET x86_64-windows-gnu)
set(CMAKE_CXX_COMPILER_TARGET x86_64-windows-gnu)
set(CMAKE_AR "${CMAKE_CURRENT_LIST_DIR}/zig-ar${voyemu_zig_wrapper_suffix}")
set(CMAKE_RANLIB "${CMAKE_CURRENT_LIST_DIR}/zig-ranlib${voyemu_zig_wrapper_suffix}")
set(CMAKE_RC_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-rc${voyemu_zig_wrapper_suffix}")
# Zig emits debug info and a PDB by default; Release builds carry neither.
set(CMAKE_C_FLAGS_INIT -g0)
set(CMAKE_CXX_FLAGS_INIT -g0)
set(CMAKE_EXE_LINKER_FLAGS_INIT -s)
set(CMAKE_SHARED_LINKER_FLAGS_INIT -s)
set(CMAKE_MODULE_LINKER_FLAGS_INIT -s)
