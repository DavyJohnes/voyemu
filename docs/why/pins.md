# Pins for external inputs

## Decision

Every external input has an exact version, URL and SHA-256. Tools and downloads are pinned in `src/app/toolchain.json`, `src/prepare/toolchain.json`, `src/app/qnx-host.json`, `src/app/dependencies.lock.json` and `component.json` files; Android images and device imports are pinned in `profile.json` with an explicit package or path. A new dependency takes its latest release unless a reason is recorded. Files of the repository itself are not pinned.

## Context

Preparing a machine combines the Android SDK and emulator, NDK, JDK, Java libraries, QEMU sources, third-party C++ sources and OEM firmware. A silently different archive changes the guest images that a machine contains. C++, Java, SDK tools and OEM inputs have no common package manager.

## Alternatives

**Version ranges or "latest" at download time.** The same command produces different machines on different days.

**A lock file of one package manager.** No single package manager covers SDK archives, QEMU sources, Java libraries and OEM images.

**Hashes of repository files as well.** Every edit then requires a hash update, and tests drift towards checking text instead of behavior.

## Argument

A mismatched SHA-256 stops preparation before a wrong input reaches a machine. Offline mode works from retained pinned files. CI downloads tools by the same pins as a local build. Repository files are reviewed through version control and verified by [behavior tests](behavior-tests.md), so a hash adds friction without adding evidence.

## Consequences

Updating a dependency changes its version, URL and SHA-256 together. OEM inputs are never in the repository; a profile records their hashes and the user imports them. The [Zig toolchain](zig-toolchain.md) and the [Java preparation](java-preparation.md) libraries follow the same rule. [Pins](../dev/pins.md) lists the pin files and the update steps.
