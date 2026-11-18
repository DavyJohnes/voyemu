# Single implementation

## Decision

All hosts share one C++ implementation. Host differences live behind narrow interfaces: `src/app/os/` for files, network, processes and volumes, `src/app/runtime/session_*_posix`, `session_*_windows`, `session_macos` and `session_linux` for the parts of a session that reach Android and QNX differently, such as the control channel, the Android transport and the QMP channel, and `src/app/platform/window_shell_*` for the window. Each host compiles only its own backend files.

## Context

Voyemu runs on macOS, Windows and Linux. A feature is described once for all platforms, so its behavior must not depend on the host beyond what the operating system forces.

## Alternatives

**A separate implementation of each feature per host.** Behavior diverges, a test on one host says nothing about the other, and every fix is made twice.

**Preprocessor branches inside feature code.** Host differences spread across many files and stay invisible to a reviewer who reads the code for one host.

## Argument

Tests of shared code run on any host, including Linux CI, and cover the behavior of every host. A backend file is short and lists exactly what differs between hosts. A new host needs new backend files, not changes throughout the feature code.

## Consequences

New host-specific behavior goes into an `os/` function, a session backend or a shell backend rather than a branch in feature code. Linux has its own backends; see [Linux x64 as a host](linux-target.md). The [SDL3 window](sdl3-window.md) applies the same rule.
