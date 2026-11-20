# free-11-russian-20260318-rc1

Experimental profile. End-to-end factory and cold-boot acceptance is pending.

The Russian firmware Free Sport+ 2026-03-18 RC1 on Android 11. The profile takes the OEM Android originals from a partition snapshot instead of the firmware update archive; the QNX cluster inputs remain those of Free 7.3.1, imported from its pinned firmware archive.

## Guests

| Item | Value |
| --- | --- |
| Android | 11, API 30, arm64 system image |
| Emulator memory | 4096 MiB |
| Cluster | QNX with the `sa8155-qinggan11-731` hardware adapter |
| Steering wheel | `free-2024` |
| Development kernel | `qinggan-11` |

## OEM inputs

`fetch` imports the pinned partition snapshot `free-11-russian-20260318-rc1.zip` from an absolute local path. The snapshot holds the raw `system.img` and `vendor.img` of the vehicle plus `vbmeta.img`; `fetch` extracts the Android originals declared in `profile.json` from the pinned images. The QNX originals are not in the snapshot: they are imported once from the pinned Free 7.3.1 firmware archive `update.zip` of the Chinese `free-11-chinese-20260731-7.3.1` profile, whose QNX partitions match this firmware. Preparation takes the `su` rules of the Android machine from the SELinux policies of the system_ext and vendor partitions, so `su` is restricted as in the vehicle.

```sh
./voyemu deps free-11-russian-20260318-rc1
./voyemu fetch free-11-russian-20260318-rc1 --source=/absolute/path/free-11-russian-20260318-rc1.zip
./voyemu build free-11-russian-20260318-rc1
```

## Notes

The firmware fingerprint is `qti/msmnile_gvmq/msmnile_gvmq:11/RQ3A.211001.001/47:user`, security patch 2022-10-05. The snapshot carries no rescue partition, so the OEM theme and the Dueros navigation application of the Chinese profiles are not part of this profile.
