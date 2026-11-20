# passion-11-russian-20260403-2.1.4.80

The Russian Voyah Passion firmware 2.1.4.80 of 3 April 2026 on Android 11. The multimedia is supported; the cluster is experimental.

## Guests

| Item | Value |
| --- | --- |
| Android | 11, API 30, arm64 system image |
| Emulator memory | 2048 MiB, 4 cores |
| Cluster | QNX, experimental |
| Steering wheel | `passion-2024` |
| Development kernel | None |

## OEM inputs

Import the pinned firmware archive `OIB_8155_H53Y3632802AW.zip` from a local path; `fetch` reads the OTA package inside it and extracts the images it needs. Prepare the machine with the multimedia only.

```sh
./voyemu deps passion-11-russian-20260403-2.1.4.80
./voyemu fetch passion-11-russian-20260403-2.1.4.80 --source=path/to/OIB_8155_H53Y3632802AW.zip
./voyemu build passion-11-russian-20260403-2.1.4.80 --cluster=off --multimedia=on
```

## Notes

The same multimedia-only `build` into an existing stopped machine updates only its Android system and keeps the writable disks, see [preparation](../how/preparation.md). The cluster recipes are under development and are not part of the supported scope.

At power-on the window plays the three OEM startup videos of `ifs_video.img` inside `ifs2_la.img` together, as the vehicle does: `proc/boot/startup_video_screen_2.mp4` on the primary screen and `proc/boot/startup_video_screen_3.mp4` on the secondary screen for up to 5 s, and `proc/boot/startup_video_screen_0.mp4` in the cluster area for up to 3 s when the window shows the cluster; no separate import is needed. After its video the primary screen shows the OEM boot animation until Android is ready. The profile imports only the boot animation of the primary screen, so the secondary screen stays black after its video, see [the window](../how/window.md#startup-screen).

The vehicle has a fourth screen, 1280×720: the second QNX screen, with the screen role `hud`. The [screens window](../how/window.md#screens) shows it with its OEM startup video `proc/boot/startup_video_screen_1.mp4` for up to 3 s, then black, because the QNX output to this screen is not emulated yet.
