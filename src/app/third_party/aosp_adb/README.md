# AOSP ADB sources

These are the Android Open Source Project sources of the host ADB protocol, Android-base, cutils and logging that the Voyemu [embedded ADB](../../adb/) builds on. Each top directory mirrors one upstream project at the revision recorded in [sources.json](sources.json): `adb` is `platform/packages/modules/adb`, `base` is `platform/system/libbase`, `cutils` is `platform/system/core` and `log` is `platform/system/logging`; a file path below that directory equals its upstream path.

Unmodified files carry their size and SHA-256 in `sources.json`, and CMake verifies them before building `voyemu_embedded_adb`. Files marked `modified` are adapted for Voyemu: they record the SHA-256 of the upstream file they derive from and carry a modification notice below their license header. The sources keep their original copyright headers under the Apache License 2.0; see [NOTICES.txt](NOTICES.txt).

Only the emulator transport is vendored: the USB transport, the macOS IOKit scanner, the epoll fdevent backend and RSA key authentication are absent. The Windows socket layer and the host API live in `src/app/adb/`.
