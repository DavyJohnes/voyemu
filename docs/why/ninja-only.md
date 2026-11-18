# Ninja only

## Decision

Voyemu configures only with the Ninja generator; CMake stops with an error for any other generator. Ninja is pinned in `src/app/toolchain.json` for each host.

## Context

The host code builds on macOS, Windows and Linux with the [Zig toolchain](zig-toolchain.md). clang-tidy reads `compile_commands.json`, and CI and local builds should run the same commands.

## Alternatives

**The Visual Studio and Xcode generators.** They are tied to MSVC and Xcode, which the Zig toolchain replaces, and they do not produce `compile_commands.json`.

**Unix Makefiles.** No native make on Windows, and a second generator doubles the configurations to keep working.

**Any generator the user picks.** Each one needs its own testing and brings its own differences in dependency tracking and custom commands.

## Argument

One small pinned binary behaves the same on every host and builds in parallel by default. One generator means one set of build files to reason about in CI and locally. It also produces the compilation database that clang-tidy needs.

## Consequences

`cmake` and `ninja` must be on `PATH`; configuration passes `-G Ninja`. Switching from another generator needs a fresh build directory. Custom targets such as `format-check`, `tidy-check` and `errorprone-check` work the same everywhere. [Build](../dev/build.md) gives the configure and build commands.
