# passion-11-russian-20260403-2.1.4.80

The Russian Voyah Passion firmware 2.1.4.80 of 3 April 2026 on Android 11. Both build targets use the same H53 firmware originals and shared recipes.

## Guests

| Target | Android ABI | Emulator | Cluster |
| --- | --- | --- | --- |
| `arm` | `arm64-v8a` | 2048 MiB, 4 cores | QNX, experimental |
| `x86` | `x86_64` | 4096 MiB, 4 cores | None |

Both targets use API 30 and the `passion-2024` steering wheel.

## Linux x86_64 target

The `x86` target runs the multimedia on Linux x64. KVM executes the Android guest and the host GPU driver renders graphics. Preparation checks the two OEM home screens and their Android Binder services. This target includes the Voyemu MCU adapter for the battery state of charge and separate power control for each multimedia display.

The signed system image carries the firmware's activity policy for older Android applications. Both OEM HOME activities share a process. Selecting the second activity leaves the other display's HOME resumed.

Playback uses the local PulseAudio socket and microphone capture stays disabled. The default socket is `$XDG_RUNTIME_DIR/pulse/native`. To use another local socket, set `PULSE_SERVER=unix:/path/to/socket`. With no available socket Android starts without audio. The firmware sets playback volume to its minimum at cold boot. Raise it with the normal volume controls to hear sound.

The build uses API 30 `default` x86_64 r11. A normal reboot of r11 succeeds. After the standard Android remount, Android Verified Boot rejects the changed boot metadata on restart before preparation copies the first OEM file. The target assembles the system files before generating integrity trees and signing the images with a local development key. It uses the pinned ARM Native Bridge files from Google APIs r16 because `MonzaCarService` needs its ARM64 JNI library to register the service that both OEM home screens use. See [preparation](../how/preparation.md#build) and [profiles as sources, machines as results](../why/profiles-machines.md).

Before signing the system image, the profile sets `ro.build.display.id` to `V2.1.4.80-20260403174306` from the H53 firmware. Android makes this ID available through `Build.DISPLAY`.

### Prepare and run

[Build the launcher from source](../dev/build.md) with the pinned Linux build tools, including JDK 25. Run the commands from the checkout root in a hardware-accelerated graphical session with access to `/dev/kvm` and the GPU render device. The SDK Emulator needs the Qt/X11 runtime libraries, including `libxkbfile.so.1`, `libSM.so.6` and `libICE.so.6`. Image preparation uses e2fsprogs, binutils, `jq`, `unzip` and the `openssl` command. Preparation downloads pinned tools with `curl`, extracts them with `dpkg-deb` and runs `avbtool` on Python 3.

Use the local path to `OIB_8155_H53Y3632802AW.zip`. Both `deps` and `build` use the developer JDK for the [Java preparation helper](../dev/ci.md#the-java-helper-workflow) until an archive with support for build targets is pinned. The example selects `x86` explicitly. With usable KVM, omitting `--target=x86` selects the same target and output directory.

Building the HAB component from source requires Android NDK r29 (`29.0.14206865`). Separately from `deps`, download the complete Linux NDK archive using the URL and SHA-256 in `sdk.archives.linux.ndk` in [`src/app/toolchain.json`](../../src/app/toolchain.json). Verify the download, extract it outside the managed SDK directory and set `NDK_HOME` to the absolute canonical path of the directory containing `source.properties`. Voyemu checks the external NDK inventory against its pin.

```sh
profile=passion-11-russian-20260403-2.1.4.80
firmware=/path/to/OIB_8155_H53Y3632802AW.zip
jdk="$PWD/.local/toolchains/jdk-25.0.5+8"
machine=machines/${profile}-x86
export NDK_HOME=/path/to/android-ndk-r29

./voyemu deps "$profile" --jdk "$jdk"
./voyemu fetch "$profile" --source="$firmware"
./voyemu build "$profile" --target=x86 --jdk "$jdk"
./voyemu check "$machine"
./voyemu start "$machine"
./voyemu status "$machine"
```

Repeat `status` to watch Android boot. Stop the session when finished. The machine directory and its writable disks remain available for the next start.

```sh
./voyemu stop "$machine"
```

### Run a signed image directly

For a standalone Android development run, set `RELEASE` to the directory of a locally signed image and supply the trusted public key and its SHA-256 SPKI fingerprint independently. `run-image.sh` verifies the image and creates a fresh two-display AVD outside the Voyemu session lifecycle. It starts Android Emulator with KVM and host graphics on the current X display. The script uses `ss` to identify its private ADB listener. It needs an unused even console port and a new run directory beneath an existing directory. It prints the private ADB command and writes the boot log to `emulator.log` in that directory.

```sh
src/dev/avd/prepare-tools.sh
src/dev/avd/run-image.sh "$RELEASE" "$PUBLIC_KEY" "$TRUSTED_SPKI_SHA256" \
    "$RUN_DIR" 5584
```

## OEM inputs

Import the pinned firmware archive `OIB_8155_H53Y3632802AW.zip` from a local path. `fetch` reads the OTA package inside it and extracts the shared OEM inputs for both targets. The inner TGZ can also be supplied directly under the filename in the update manifest. The command verifies its byte count and SHA-256.

On Apple Silicon, build the `arm` target with the [common preparation steps](../how/preparation.md). To prepare only multimedia, select `--cluster=off`. The machine goes to `machines/passion-11-russian-20260403-2.1.4.80-arm` by default.

```sh
./voyemu build passion-11-russian-20260403-2.1.4.80 --target=arm --cluster=off --jdk "$jdk"
```

## Notes

On the `arm` target, a multimedia-only `build --force` into an existing stopped machine updates its Android system and keeps the writable disks. A signed `x86` build uses a new machine directory. See [preparation](../how/preparation.md).

The application list shows the nine tiles of the vehicle in its order: Vehicle, Bluetooth Phone, Scenes, 360 View, Drive Recorder, Camera, Photos, Music and Video. Each tile opens the OEM application of the firmware: `com.voyah.scenemode` for Scenes, `com.mega.dvr` for Drive Recorder and `com.voyah.gallery` for Photos among them. Photos and Scenes run with the system rights they have in the vehicle, so Photos reads the photos and videos of the internal storage and of the USB drives. Drive Recorder shows the virtual camera, see [Passion Drive Recorder](../how/camera.md#passion-drive-recorder).

At power-on the window plays the three OEM startup videos of `ifs_video.img` inside `ifs2_la.img` together, as the vehicle does: `proc/boot/startup_video_screen_2.mp4` on the primary screen and `proc/boot/startup_video_screen_3.mp4` on the secondary screen for up to 5 s, and `proc/boot/startup_video_screen_0.mp4` in the cluster area for up to 3 s when the window shows the cluster; no separate import is needed. After its video the primary screen shows the OEM boot animation until Android is ready. The profile imports only the boot animation of the primary screen, so the secondary screen stays black after its video, see [the window](../how/window.md#startup-screen).

The vehicle has a fourth screen, 1280×720: the second QNX screen, with the screen role `hud`. The [screens window](../how/window.md#screens) shows it with its OEM startup video `proc/boot/startup_video_screen_1.mp4` for up to 3 s, then black, because the QNX output to this screen is not emulated yet.
