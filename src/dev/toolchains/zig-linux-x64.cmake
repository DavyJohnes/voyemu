# Native Linux toolchain on the pinned Zig 0.15.2 with a glibc 2.34 baseline, the oldest glibc of
# the supported distributions (RHEL 9; Ubuntu 22.04 and Debian 12 are newer); see docs/dev/build.md.
# Use with -G Ninja -DCMAKE_TOOLCHAIN_FILE="$PWD/src/dev/toolchains/zig-linux-x64.cmake" from the project root.
set(CMAKE_C_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-cc")
set(CMAKE_CXX_COMPILER "${CMAKE_CURRENT_LIST_DIR}/zig-c++")
set(CMAKE_C_COMPILER_TARGET x86_64-linux-gnu.2.34)
set(CMAKE_CXX_COMPILER_TARGET x86_64-linux-gnu.2.34)
set(CMAKE_AR "${CMAKE_CURRENT_LIST_DIR}/zig-ar")
set(CMAKE_RANLIB "${CMAKE_CURRENT_LIST_DIR}/zig-ranlib")
# Zig emits debug info and a PDB by default; Release builds carry neither.
set(CMAKE_C_FLAGS_INIT -g0)
set(CMAKE_CXX_FLAGS_INIT -g0)
set(CMAKE_EXE_LINKER_FLAGS_INIT -s)
set(CMAKE_SHARED_LINKER_FLAGS_INIT -s)
set(CMAKE_MODULE_LINKER_FLAGS_INIT -s)
