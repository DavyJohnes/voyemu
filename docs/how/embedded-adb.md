# Embedded ADB

The supervisor talks to the Android guest of a session through its own ADB client, built into the launcher. It connects over loopback to the `adbd` endpoint of the emulator that this session started and uses no ADB server shared with other programs. The reasons are in [embedded ADB](../why/embedded-adb.md).

Voyemu also registers its Android guest on the standard local ADB server when the session starts. The guest appears as `emulator-<console-port>` in `adb devices` and disappears when Android stops. The supervisor keeps using its embedded client for its own operations.

## What goes through it

The shell commands and long-running shell streams that the session runs inside Android use this transport; before each use the supervisor checks that the emulator process and the guest generation are still the ones it owns. A launch of a prepared machine always selects the embedded transport.

## Ownership

One supervisor process owns one emulator transport. Before connecting, the supervisor checks that the endpoint belongs to the emulator process it started and to its console and `adbd` listeners, so another emulator on the same host cannot answer for the guest. After a warm reboot the client opens a new connection to the guest of the new generation.

## Where the SDK adb is still used

Machine preparation and imports from a physical vehicle with `fetch PROFILE --source=adb --serial SERIAL` use the pinned `adb` from Platform Tools. The embedded client has no USB transport.

The client consists of Voyemu code in `src/app/adb/` and AOSP ADB sources in `src/app/third_party/aosp_adb/`; CMake verifies the unmodified AOSP files against their pins before building.
