# Size-optimized Release

## Decision

Release compiles the launcher with `-Os` and puts every function and data object in its own section; the sources on the per-frame path of the window compile with `-O2`. Link-time optimization stays off. [Build](../dev/build.md#optimization) names the flags and the per-frame sources.

## Context

The launcher is one static executable with the CLI, preparation, the Voyemu API, the window and the graphics worker. Most of its code runs once per command or waits for I/O, the Java preparation and the guests, which QEMU and the emulator run. Per frame, at up to 60 frames per second, the launcher converts QNX frames, receives Android frames, decodes the PNG frames of the boot animation and the startup videos and, in the graphics worker, replays QNX drawing through ANGLE. Pin checks hash with the SHA-256 instructions of the CPU where it has them, so their speed hardly depends on the optimization level.

## Alternatives

**`-O3` everywhere**, the CMake default for Release. The largest executable; the speed it buys goes to code that waits.

**`-Os` everywhere.** The smallest executable, but the conversion of a QNX frame takes about twice as long, which counts against a frame budget of 16.7 ms.

**`-O2` with sections everywhere.** It saves about a quarter of what `-Os` saves.

**ThinLTO.** On top of `-Os` it saves another 3 % on Linux. The Zig Mach-O linker refuses link-time optimization, so macOS would build differently from Windows and Linux. Every test executable would link through LTO, and Zig passes no option that bounds the LTO threads of ld.lld.

**Identical code folding on ld.lld.** Zig 0.15.2 rejects `--icf` among the linker options. A link command outside Zig would repeat what Zig derives for each target: startup files, the C and C++ runtimes and the libc stubs.

## Argument

Size is saved where speed does not matter, and speed is kept where the frames are. The per-frame list is short and names sources, so a change to it shows in review.

## Consequences

Every source on the per-frame path of the window belongs to `voyemu_frame_path_sources` in `src/app/CMakeLists.txt` or to its SDL3 filter. Preparation runs slightly slower than at `-O3`. lld-link folds identical functions on Windows by its default; Linux and macOS go without folding until Zig passes such an option. The flags are part of the [Zig toolchain](zig-toolchain.md) and of [Release only](release-only.md).
