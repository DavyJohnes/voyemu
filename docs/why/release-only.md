# Release only

## Decision

Voyemu builds only Release. CMake forces `CMAKE_BUILD_TYPE=Release`; the toolchain files compile with `-g0` and strip at link, so binaries carry no debug information and no PDB. Failures are diagnosed from logs, not with a debugger.

## Context

A Voyemu session runs the supervisor, the emulator, QEMU and two guests for minutes at a time. Failures depend on timing between these processes. Users run the binary from a release or a CI build.

## Alternatives

**Debug and RelWithDebInfo configurations.** A second configuration doubles the build and test matrix and changes the timing of the code under test. A defect that appears only in Release stays invisible in the configuration developers use.

**Release with separate symbol files.** PDB or dSYM files must be produced, stored and matched to each binary. A debugger that pauses the supervisor also changes the timing it investigates.

## Argument

The tested binary is the shipped binary. Timing-dependent faults across processes are reconstructed from ordered log records rather than from a paused process. One configuration keeps local builds, CI and releases identical.

## Consequences

Every failure path must leave enough context in the log to diagnose it: a failure that the log cannot explain calls for more logging, not a debug build. The host writes structured JSON log records to `stderr`. CI checks that the Windows build produces no PDB. Binaries are smaller without debug sections. The toolchain settings are part of the [Zig toolchain](zig-toolchain.md). [Logs](../how/logs.md) describes the log records and session log files.
