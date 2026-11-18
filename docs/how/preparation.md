# Preparation

Preparation turns a [profile](profiles-machines.md) and its OEM inputs into a machine in three commands: `deps` installs pinned tools, `fetch` imports and verifies OEM inputs, and `build` prepares the machine. Each step checks the [pins](../why/pins.md) of what it uses and stops on a mismatch.

```sh
./voyemu deps free-09-chinese-20260106-6.0.3
./voyemu fetch free-09-chinese-20260106-6.0.3 --source=/absolute/path/update.zip
./voyemu build free-09-chinese-20260106-6.0.3
```

Run the launcher from the installation root; on Windows the launcher is `voyemu.exe`. The installation root holds the launcher and `profiles/`; the default preparation path needs no `src/`, see [release](../dev/release.md#project-files). The launcher comes from a release archive or is built from source in the project root, see [build](../dev/build.md). Preparation of a QNX machine needs a launcher built with the graphics worker; the release archives have it.

## deps

`deps [PROFILE]` installs the pinned host tools into `.local/toolchains/`: the Android Emulator, Platform Tools, Build Tools, the SDK CMake and Ninja, and the QEMU engine for QNX. With a profile it also prepares that profile's Android base when the profile needs one. `deps` also installs the Android platform JARs and the Java tools of preparation, which CI builds and publishes: a minimal Java runtime of the host, the Java helper and the guest Java code of the profiles, see [CI](../dev/ci.md). No JDK is needed; [Java preparation](../why/java-preparation.md) explains why. By default the pinned helper asset is used even in a source checkout. If it is unavailable, preparation stops with `JAVA_HELPER_ASSET_MISSING`; developers can explicitly select the source recipe with `--jdk`, see [Java helper CI](../dev/ci.md#the-java-helper-workflow).

Downloads go over the host's HTTPS stack and certificate store: WinHTTP on Windows, libcurl on macOS and Linux. On Linux Voyemu carries no TLS or cryptography of its own: it loads the system libcurl (`libcurl.so.4`) for downloads and OpenSSL 3 (`libcrypto.so.3`) for APK signing in `build`, and stops with `DOWNLOAD_LIBRARY` or `APK_CRYPTO_LIBRARY` naming the missing library. When a server answers HTTP 429 or 503, Voyemu repeats the request after its `Retry-After` delay (at most five minutes) or after a backoff from 2 to 64 seconds, up to seven attempts; files already downloaded stay in the cache, so a repeated command continues where it stopped.

While the `prebuilt` pin of the QNX engine is pending, `deps` builds the engine from its recipe, see [the qnx-host workflow](../dev/ci.md#the-qnx-host-workflow): on macOS with Clang and `/usr/bin/python3` 3.9 or newer from the Command Line Tools, which `xcode-select --install` installs, and the Ninja of the SDK CMake package; on Linux with the pinned Zig, `/usr/bin/python3` 3.9 or newer, `/usr/bin/make` and the Ninja of the SDK CMake package. On Windows `deps` installs the engine only from its published asset or a verified ready-to-run engine of its local cache; otherwise it reports the engine unavailable and machines run without the cluster. An installed engine of the same recipe is reused. On Windows `build` formats the cache image of a machine with `mke2fs.exe` and `mke2fs.conf` of Platform Tools, and the prepared machine carries the formatted `cache.img`.

`--jdk PATH` runs preparation on a JDK 25 or newer instead of the Java runtime and can compile guest Java code from the profile. In a source checkout it also selects the developer recipe for the helper; the option is meant for development.

## fetch

`fetch PROFILE` imports or verifies the OEM inputs of a profile in `.local/profiles/<profile>/`.

| Form | Effect |
| --- | --- |
| `fetch PROFILE` | Verifies the inputs already retained for the profile |
| `fetch PROFILE --source=/absolute/update.zip` | Imports a pinned firmware archive from an absolute local path |
| `fetch PROFILE --source=adb --serial SERIAL` | Reads the Android files declared in `original` of `profile.json` from a physical vehicle over authorized ADB; a profile with `status` `incomplete` reads them by `capture.json` |

The archive and the files are checked against the sizes and SHA-256 in the profile before anything is published. Each [profile page](../profiles/index.md) names its input route and OEM file names. The launcher imports an archive itself, and an import from a vehicle uses the pinned SDK `adb`; the capture of an incomplete profile runs on the Java tools that `deps` installs.

## build

`build PROFILE [MACHINE_PATH]` prepares a new machine, `machines/PROFILE` by default. It applies [components and recipes](components.md), builds the guest images, boots the guests for verification and writes `runtime.json` last. A failed verification boot of QNX ends the QEMU and graphics processes that `build` started and removes their RAM and image copies. On success it prints the machine and the path of its `runtime.json`.

The QNX guest images take CI-built binaries: `build` downloads them by their pins, checks that the OEM libraries of the profile export everything they import and, for Free 6.0.3, writes the OEM startup script and `sysmgrtool` into the section the front library reserves for them, as data and without a compiler. See [the qnx-guest workflow](../dev/ci.md#the-qnx-guest-workflow).

The QNX guest shell scripts are installed without comments, indentation, trailing blanks and blank lines, so that each fits the OEM file it replaces; the scripts in the repository keep them. Preparation refuses a guest script with a here-document (`<<`).

The native Android components of a profile are CI-built as well: `build` downloads each by its pin and hands it to the Java helper; no NDK takes part. A library that imports from libraries of the Android image, such as the multi-display library, links with those imports undefined, and `build` checks that the libraries of the image export them. See [the android-native workflow](../dev/ci.md#the-android-native-workflow).

The QNX host image keeps the OEM bytes of every unchanged 64 KiB block of its boot stream; Voyemu packs the changed blocks itself and, on Free 6.0.3, sizes them so that the startup accepts the stream. See [own LZO1X packer](../why/lzo-packer.md).

`--cluster=on|off` and `--multimedia=on|off` select the subsystems; both are on by default. `--cluster=off --multimedia=on` prepares only the multimedia, and a cluster-only build is rejected before preparation. For profiles that support it, an Android-only build into an existing stopped machine updates its Android system and keeps the writable disks and an existing cluster.

A fresh build refuses an existing destination. Preparation checks free space before each storage-heavy stage; tools, OEM inputs, intermediate files and writable disks need far more space than the firmware archive alone.

Profiles using `qg-runtime` keep emulated NVRAM in `/data/user_de/0/com.voyemu/nvram` inside the guest. The first caller atomically creates it from the read-only `/system/etc/nvram` seed; later calls from OEM processes share the same file, which survives a cold boot in the machine's writable Android disk. Clearing the Android app data of `com.voyemu` removes this state, so the next access creates it from the seed again.

## Offline preparation

`--offline` on `deps`, `fetch` and `build` uses only files already in the `.local` roots and fails when a pinned input is missing. Successful preparation does not prove that every OEM application works; that is checked by acceptance on a running machine.
