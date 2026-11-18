# Voyemu

Voyemu emulates Android-based Voyah vehicles (Free, Dreamer, Passion) on macOS, Windows and Linux. It runs the original multimedia (Android, two screens) and instrument cluster (QNX) firmware, downloaded from the user's vehicle or taken from OTA update files; no firmware is included. Voyemu emulates steering-wheel button presses and vehicle parameter input. Through the [Voyemu API](how/api.md), scripts take screenshots, read the UI element tree, send input and observe CAN messages.

A firmware profile describes the original inputs and their adaptations; a prepared machine contains the guest images, resources and writable disks.

## Why Voyemu

### Real firmware instead of a mock

The guests are the vehicle's own Android and QNX images, imported by the user from the firmware they have. Adaptations are recipes with a `guard` on the exact original bytes they change, and OEM package identities stay unchanged. OEM files never enter the repository.

### The profile is the source, the machine is the result

A profile in `profiles/` describes one firmware: its OEM inputs, recipes, components and pins. Preparation turns it into a machine that embeds everything a launch needs, so a prepared machine runs without the authoring profile and moves as one directory. See [profiles and machines](why/profiles-machines.md).

### Two operating systems in one window

The cluster and both multimedia screens appear side by side with steering-wheel buttons and vehicle controls. The Android and QNX guests talk over an interguest link, as they do in the vehicle. See [the SDL3 window](why/sdl3-window.md).

### Reproducible preparation

Every external download has an exact version, URL and SHA-256, and a mismatch stops preparation before a wrong input reaches a machine. See [pins for external inputs](why/pins.md).

### Persistent guest data

Installed applications, files and settings live on the machine's writable disks. They survive a cold boot and a copy of the machine directory, while ephemeral session state stays in `.local/`. See [four `.local` roots](why/local-roots.md).

### Automation with explicit outcomes

The Voyemu API drives sessions: ownership, vehicle state, input, UI observations, screenshots and waits. Every failure says whether the operation did not start or its outcome is unknown, and an unknown outcome forbids an automatic retry. See [the Voyemu API](why/api.md) and [error codes](why/error-codes.md).

### One implementation for every host

macOS, Windows and Linux share one C++ implementation with thin host backends, built by one pinned Zig toolchain. A feature behaves and is described the same on every platform. See [single implementation](why/single-implementation.md) and [the Zig toolchain](why/zig-toolchain.md).

## Where to go next

- [How it works](how/index.md) — profiles, machines, preparation, sessions, guests and the Voyemu API.
- [Profiles](profiles/index.md) — the published profiles and their OEM inputs.
- [Development](dev/index.md) — building, tests, checks, CI and pins.
- [Why this way](why/index.md) — design decisions and their rationale.
- [Glossary](GLOSSARY.md) — the terms used across the documentation.
