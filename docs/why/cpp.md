# C++ for the host

## Decision

The Voyemu host is written in C++20, and the code that runs inside the QNX guest is written in C11. APK and DEX preparation runs in [Java](java-preparation.md). Rust is not used.

## Context

The launcher builds third-party C and C++ code from pinned sources: SDL3 with its stb_image, Dear ImGui, nlohmann/json, miniz, lzokay, the libarchive headers and the AOSP sources of the host ADB protocol in `src/app/third_party/aosp_adb/`, on which [embedded ADB](embedded-adb.md) builds. The e2fsprogs image helpers are C programs on libext2fs from the pinned e2fsprogs release. The QNX engine is QEMU with a patch to its C sources, `src/app/qnx-host.patch`. The host interfaces that the launcher calls are C, Objective-C and COM: Cocoa and VideoToolbox on macOS, Win32 and Media Foundation on Windows.

The code in `src/guest/qnx/` runs inside the OEM QNX 7.0 images on aarch64. Preparation compiles it with the clang of the pinned Zig for the target `aarch64-unknown-qnx7.0.0` without the QNX SDP: the objects see only the compiler and staged headers, and the libraries link against stubs that carry only the sonames of the guest `libc.so.4` and `libscreen.so.1`. Preparation checks guest binaries against byte bounds. The host and the guest share wire structures: the host includes the C header `src/guest/qnx/cluster_ui_wire.h` and checks its sizes and offsets with `static_assert`, and host tests compile guest C sources directly.

Rust lists QNX SDP 7.0 on aarch64 as `aarch64-unknown-nto-qnx700` at tier 3. Its [platform support](https://doc.rust-lang.org/rustc/platform-support/nto-qnx.html) marks the full standard library for this target as in progress; only `no_std` is supported. Rust ships no prebuilt artifacts for the target, and the build calls `qcc` from an installed QNX SDP to link.

## Alternatives

**A Rust host with bindings.** SDL3, Dear ImGui, the AOSP ADB code, libext2fs and the host interfaces each need bindings; the C++ interfaces of Dear ImGui and AOSP ADB need a C wrapper first. Every call across a binding is `unsafe`, so the boundary with these libraries keeps the risks of C. The existing host would be rewritten. Rust brings its own compiler and Cargo, a second toolchain to pin on every host next to Zig, which still builds the guest code and the helpers.

**Rust for the QNX guest code.** The only QNX 7.0 target for aarch64 is tier 3 with `no_std`, and it needs the QNX SDP and `qcc`, which Voyemu does without. The wire structures would exist twice, in C for the host and in Rust for the guest.

**Mixed C++ and Rust in the host.** Two compilers, a Cargo build inside CMake for three targets and a C interface between the two halves. clang-tidy checks only the C++ half, so the Rust half needs its own checks.

**Go.** Every C library goes through cgo, and Dear ImGui and AOSP ADB need a C wrapper as well. Go adds a garbage-collected runtime and a second toolchain. Go has no QNX port, so the guest code stays in C.

## Argument

The libraries, the host interfaces and the guest are C and C++, so C++ calls them without bindings, and the host and the guest share C headers as is. One [Zig toolchain](zig-toolchain.md) builds the launcher, the QNX guest code and the e2fsprogs helpers for macOS, Windows and Linux; Rust would add a toolchain without removing one.

Checks guard against the memory errors of C and C++:

- clang-tidy runs the `bugprone` group with warnings as errors on every Voyemu C, C++ and Objective-C++ source, in CI on a Linux tree and on a Windows cross tree; see [checks](../dev/checks.md).
- The core libraries of the launcher compile with `-Wall -Wextra -Wpedantic`, and the guest code with `-Wall -Werror`.
- Test executables compile without `NDEBUG` in the Release build, so `assert` checks run; see [tests](../dev/tests.md).
- The launcher compiles with `-Os` and the per-frame path of the window with `-O2`, see [size-optimized Release](size-optimized-release.md). Undefined behavior can show differently at another optimization level: the move to `-Os` made host tests catch a loop over a destroyed temporary JSON object.
- The OEM formats that the launcher parses come from inputs with SHA-256 [pins](pins.md).

The build has no sanitizer configuration.

## Consequences

New host code is C++20 and follows [the single implementation](single-implementation.md); new guest code is C11 and builds with the same Zig. C and C++ libraries join without a binding layer. Memory safety depends on the checks and tests above rather than on the compiler. Rust can be reconsidered for a new isolated part of the host with a narrow C interface, such as the parser of a new format, once the gain outweighs a second pinned toolchain, or for the guest once Rust has a supported target for QNX 7.0 on aarch64.
