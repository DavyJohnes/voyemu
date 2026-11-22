# free-11-russian-20260318-rc1

Experimental profile. End-to-end factory and cold-boot acceptance is pending.

The Russian firmware Free Sport+ 2026-03-18 RC1 on Android 11. The profile takes the OEM Android originals from a partition snapshot instead of the firmware update archive. The snapshot has no QNX images, so the profile builds without the cluster.

## Guests

| Item | Value |
| --- | --- |
| Android | 11, API 30, arm64 system image |
| Emulator memory | 4096 MiB |
| Cluster | None |
| Steering wheel | `free-2024` |
| Development kernel | `qinggan-11` |

## OEM inputs

`fetch` imports the pinned partition snapshot `free-11-russian-20260318-rc1.zip` from an absolute local path. The snapshot holds the raw `system.img` and `vendor.img` of the vehicle plus `vbmeta.img`; `fetch` extracts the Android originals declared in `profile.json` from the pinned images. Preparation takes the `su` rules of the Android machine from the SELinux policies of the system_ext and vendor partitions, so `su` is restricted as in the vehicle.

```sh
./voyemu deps free-11-russian-20260318-rc1
./voyemu fetch free-11-russian-20260318-rc1 --source=/absolute/path/free-11-russian-20260318-rc1.zip
./voyemu build free-11-russian-20260318-rc1
```

## Notes

The firmware fingerprint is `qti/msmnile_gvmq/msmnile_gvmq:11/RQ3A.211001.001/47:user`, security patch 2022-10-05. The snapshot carries no rescue partition, so the OEM theme and the Dueros navigation application of the Chinese profiles are not part of this profile.

The machine has no cluster, so `start` runs only the multimedia, see [launch and sessions](../how/launch.md). The cluster can be added once the QNX images are captured from a vehicle with `voyemu fetch free-11-russian-20260318-rc1 --source=adb --serial SERIAL` and their pins are contributed, see [contributing](../../CONTRIBUTING.md).

The Camera tile opens the ordinary camera with a virtual camera; as in the vehicle, the application list has no DVR tile, see [camera and DVR](../how/camera.md).

The Video tile opens the OEM Video application `com.qinggan.app.video`; preparation adds to it the same pinned ARM64 libraries as to the OEM Gallery, so the application runs on ARM64 hosts without 32-bit support.
