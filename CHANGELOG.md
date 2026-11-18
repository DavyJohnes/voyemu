# Changelog

English | [Русский](CHANGELOG.ru.md) | [README](README.md)

## 1.0.0-beta1 — 2026-11-17

First public release. Voyemu runs a vehicle's Android multimedia applications and QNX instrument cluster on macOS, Windows and Linux from firmware profiles. On Windows the cluster needs the CI-built QNX engine, which is not published yet; until then machines run without the cluster.

- Profiles: Free 6.0.3 `free-09-chinese-20260106-6.0.3`, Free 6.11.1 `free-11-chinese-20250710-6.11.1` and Free 7.3.1 `free-11-chinese-20260731-7.3.1` with multimedia and cluster; Free NXP RC45 `free-09-chinese-20221230-nxp-rc45` and Passion `passion-11-russian-20260403-2.1.4.80` with multimedia. The Passion cluster is experimental. The Free profiles run on the Free/Dreamer platform: their QNX hardware adapters `sa8155-qinggan09`, `sa8155-qinggan11-611` and `sa8155-qinggan11-731` and their error codes use its internal designation `qinggan09` and `qinggan11`.
- Preparation: pinned tools through `deps`, OEM input import through `fetch` from a local update archive or over ADB, and Android/QNX machine preparation through `build`; Free 7.3.1 and Passion support an Android-only rebuild that preserves user disks.
- Portable machines: a prepared machine embeds its profile and resources; persistent Android/QNX disks keep installed applications, files and settings across cold boots and copies of a stopped machine.
- Lifecycle: `start`, `restart`, `status`, `stop`, `check` and `patch` with positional machine paths, `--cluster=on|off` and `--multimedia=on|off`; command output in English, Russian or Chinese chosen by `--lang` or the host language.
- Unified window with the QNX cluster, two Android multimedia screens with the OEM boot animation of each screen until Android is ready, vehicle parameters and steering-wheel controls; on Free 6.0.3, 6.11.1 and 7.3.1 the cluster plays its OEM startup sequence at power-on; 50/75/100% zoom and an English/Russian/Chinese interface.
- Screens window with the other screens of the vehicle, opened by a control next to the wheel: the rear-seat screen of Free 6.11.1 and 7.3.1 with the OEM rear-screen application and its boot animation, and the second QNX screen of Passion, 1280×720, with its OEM startup video; the Voyemu API addresses them as `rear` and `hud`.
- Synthetic phone scenario with Russian contacts, `+7` incoming and outgoing calls and OEM answer/hangup input; no real calls are made.
- Cluster data channel on the Free 6.0.3, 6.11.1 and 7.3.1 profiles: one persistent connection to the cluster with the OEM heartbeat carries the phone call, media and turn-by-turn data from the OEM APIs in OEM IVC frames; the OEM `SystemPolicy` and `QGBus` imported from the firmware produce this data as in the vehicle; `cluster.ipk.history` reads the sent and received frames without their text.
- OEM service `qg.carsignal` on the Free 6.0.3, 6.11.1 and 7.3.1 profiles: one implementation answers as a parked vehicle without the multimedia MCU, so the OEM applications stop waiting for it.
- Voyemu API (`schema: 1`) through `voyemu test`: session ownership, vehicle state, input, UI observations with the live drawable state of Android views and the Kanzi node tree of the Free 6.0.3, 6.11.1 and 7.3.1 clusters with typed node properties, CAN observations, waits, logs, operation receipts and joint screenshots of all three screens; QNX frames over local gRPC.
- Pinned dependencies: every download is fixed by URL and SHA-256; a mismatch stops preparation. The development kernels with the CAN driver of the Free 6.0.3, 6.11.1 and 7.3.1 profiles are built in CI and published as release assets.
- Native C++ host built with Zig; machine preparation needs no JDK: a minimal Java runtime, the Java helper and the guest Java code are built in CI and installed by `deps`; English/Russian documentation and embedded third-party license notices.

Known limitations: the Passion cluster has no UI tree, Android observations can be partial, and command or screenshot success does not prove a visible result. Real calls, host microphone input and OEM cloud services are not part of Voyemu. Original firmware materials are not included and are not covered by the project license.
