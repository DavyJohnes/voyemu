# Zig toolchain

## Decision

Voyemu builds with the pinned Zig 0.15.2 on every host: `aarch64-macos` with the Command Line Tools SDK, `x86_64-windows-gnu` with MinGW-w64 natively on Windows or cross-built on Linux, and `x86_64-linux-gnu` with a glibc 2.34 baseline. MSVC and Xcode are not supported. The toolchain files live in `src/dev/toolchains/`.

## Context

Voyemu runs on macOS, Windows and Linux. The host code is [one shared C++20 implementation](single-implementation.md). Each host's Zig, CMake and Ninja archives are pinned by URL and SHA-256 in `src/app/toolchain.json` under `optionalTools`.

## Alternatives

**MSVC on Windows and Apple Clang from Xcode on macOS.** Two compiler front ends with different diagnostics, flags and language extensions. MSVC comes through the Visual Studio installer rather than one pinned archive and does not run on a Linux CI runner.

**A MinGW-w64 GCC or LLVM-MinGW distribution.** It covers Windows only; macOS and Linux would still need another compiler.

**The system compiler of each host.** Its version follows the host, so a build stops being reproducible from the pins.

## Argument

One archive per host gives one Clang-based compiler and one set of flags and warnings everywhere. Zig cross-compiles, so a Linux runner builds the Windows executable and runs clang-tidy over it. The archive is a plain download and follows the [pin rule](pins.md). On macOS the toolchain file needs only the Command Line Tools SDK, which it finds with `xcrun`.

## Consequences

Configuration always passes a Zig toolchain file; switching from another toolchain needs a fresh build directory. Windows binaries use the MinGW ABI, so libraries that ship only MSVC import libraries are not usable. Zig emits debug information and a PDB by default; the toolchain files turn both off for [Release-only builds](release-only.md). Updating Zig changes `toolchain.json` and the toolchain files together. [Build](../dev/build.md) gives the configure and build commands.
