@rem Runs the pinned Zig rc; the target triple comes from the CMake toolchain file.
@rem Zig keeps its cache in .local\build\zig-cache unless the caller sets one.
@setlocal
@if not defined ZIG_GLOBAL_CACHE_DIR set "ZIG_GLOBAL_CACHE_DIR=%~dp0..\..\..\.local\build\zig-cache"
@if not defined ZIG_LOCAL_CACHE_DIR set "ZIG_LOCAL_CACHE_DIR=%ZIG_GLOBAL_CACHE_DIR%"
@"%~dp0..\..\..\.local\toolchains\zig-0.15.2\zig.exe" rc %*
