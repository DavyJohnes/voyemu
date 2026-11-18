# Embedded ADB

## Decision

During a session the supervisor talks to the Android guest through an embedded ADB client. It connects over loopback to the `adbd` port of the emulator that the session started, without an external ADB server. The client is Voyemu code in `src/app/adb/` over pinned AOSP ADB sources in `src/app/third_party/aosp_adb/`. Machine preparation and imports from a physical vehicle keep using the pinned SDK `adb`.

## Context

Standard ADB routes every command through one ADB server per user on port 5037, shared by all tools on the host. Any tool can restart that server, and the server attaches to every emulator it finds. Voyemu must prove that the guest it controls belongs to its own session: it checks the retained emulator process together with its console and `adbd` listeners. The SDK emulator also tries to notify a host ADB server.

## Alternatives

**The pinned SDK `adb` with the default server.** The server is global state: an IDE or another tool can restart it or attach the same emulator, and a session cannot prove which transport it holds.

**The SDK `adb` with a private server port per session.** The sharing disappears, but a separate server process remains with its own lifetime, reconnection and port scanning, and every command starts another client process.

## Argument

The transport lives inside the supervisor that owns the session. Its lifetime equals the session's, reconnection after a guest reboot stays under the supervisor's control, and no other process can take it over. Voyemu binds the emulator's server notification port without listening, so no external server can take that port either. The same native code builds on every host and needs neither an external ADB nor any other runtime.

## Consequences

The embedded client handles one local emulator per process and does not include the USB transport; physical devices go through the SDK `adb`. The client holds no ADB key: the pinned emulator system images run `adbd` with `ro.adb.secure=0`, and a guest that requests authentication stays unauthorized. Updating ADB means updating the AOSP sources; CMake verifies the sizes and SHA-256 of the unmodified files before building, and modified files record the AOSP revision they derive from; see [pins](pins.md). [Embedded ADB](../how/embedded-adb.md) describes how a session uses the client.
