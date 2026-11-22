# free-09-chinese-20221230-nxp-rc45

The Chinese Free firmware RC45 of 30 December 2022 for the multimedia board on NXP i.MX 8QuadMax, on Android 9. The profile is supported for the multimedia; the profile has no cluster.

## Guests

| Item | Value |
| --- | --- |
| Android | 9, API 28, arm64 system image |
| Emulator memory | 4096 MiB |
| Cluster | None |
| Steering wheel | `free-2021` with the button routes of this profile |
| Development kernel | `qinggan-09` |

## OEM inputs

Import the pinned firmware archive `update.zip` from a local path; `fetch` extracts the Android files it needs, the SELinux policies of the system and vendor images among them. Preparation takes the `su` rules of the Android machine from both policies, so `su` is restricted as in the vehicle. Prepare the machine with the multimedia only.

```sh
./voyemu deps free-09-chinese-20221230-nxp-rc45
./voyemu fetch free-09-chinese-20221230-nxp-rc45 --source=path/to/update.zip
./voyemu build free-09-chinese-20221230-nxp-rc45 --cluster=off --multimedia=on
```

## Notes

The machine has no cluster, so `start` runs only the multimedia, see [launch and sessions](../how/launch.md).
