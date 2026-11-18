# SDL3 window

## Decision

The host shows one window built on statically linked SDL3, with Dear ImGui drawing the controls through its SDL3 renderer backend. The window combines the cluster, both multimedia screens and the vehicle controls. The window has no menu: zoom, interface language, guest power and About are [controls next to the wheel](../how/window.md#controls-next-to-the-wheel). A small shell backend per host covers only what SDL3 does not expose: the standard Quit item that macOS needs for Command-Q, the title bar theme and keeping a background launch behind other windows.

## Context

Voyemu runs on macOS, Windows and Linux. The window shows guest frames, takes pointer, keyboard and steering-wheel input and switches zoom and interface language. The SDL3 source archive is pinned in `src/app/platform/component.json`; Dear ImGui is pinned in `src/app/dependencies.lock.json`.

## Alternatives

**A native window per host: Cocoa on macOS, Win32 on Windows.** Each feature is written and tested twice, and the two windows drift apart.

**A cross-platform widget toolkit such as Qt.** A large dependency with its own build system, licensing terms and deployment files for a window that mostly shows frames and a few controls.

**A separate window per screen.** The user arranges several windows, and input routing and zoom repeat per window.

## Argument

SDL3 provides windows, input and a renderer on every host from one source archive that builds with the [Zig toolchain](zig-toolchain.md). Static linking leaves no runtime library to install next to the executable. Dear ImGui keeps the controls in the same C++ code as the rest of the window. The shell backends follow the [single implementation](single-implementation.md) rule: shared behavior with thin host files.

## Consequences

Window behavior is described and tested once for all hosts. Host-specific window code stays in `platform/window_shell_*`. The window is built when `VOYEMU_BUILD_CLUSTER` is on, which is the default. Updating SDL3 or Dear ImGui is a pin change. [The window](../how/window.md) describes what the window shows.
