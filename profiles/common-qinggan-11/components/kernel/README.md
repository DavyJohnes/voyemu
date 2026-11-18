# Free/Dreamer Android 11 development kernel

Linux 5.4 GKI kernel for the API 30 emulator base of the A11 profiles, with the development CAN transport driver `cis_can_dev` and netlink protocol 21; the model device writes 10-byte CAN frames and longer service datagrams (up to 65536 bytes, `can_datagram.h`). It is a development kernel for a simulated ECU, not an OEM kernel and not a machine acceptance receipt.

## Inputs

`component.json` pins the external inputs and lists the recipe patches and driver files in order; the recipe files themselves carry no pins, `prepare` records their hashes and later steps reject a recipe edited since, and the published Image pin covers them. The source is kernel/common commit `ae78026f427c6692765ce1c2996d0a9cb6e486ef`, the stock API 30 kernel revision; the extracted tree is verified by its Git tree id and a SHA-256 manifest because Gitiles archives embed the request time. The compiler is the stock Android clang 12.0.5 r416183b build 7284624: `clang-members-<host>.json` lists the selected official Git blobs of the host compiler, including modes, symlinks, compiler headers and CFI resources; it is not a complete toolchain archive. GNU make comes from the pinned NDK and is pinned per host. The stock kernel and ramdisk are pinned for the module ABI guard.

Host tools come from `src/app/toolchain.json` (`optionalTools.kernelBuild`): GNU sed, GNU gzip and OpenSSL libcrypto built from pinned sources with the pinned Zig, and Android build-tools toybox, xz, bison, flex and m4 for the host. Zig is also the host C compiler. No Homebrew, system compiler or system OpenSSL is used.

CFI, ThinLTO, module versions, RELR, compat VDSO and SELinux remain enabled. The 37 stock vendor modules stay unchanged; every build checks all 2,594 imported symbol CRCs and the exact stock release string. A successful build does not prove runtime CFI or machine readiness.

## Build

Supported build hosts are macOS and Linux x86_64 with JDK 25 or newer, the pinned NDK and Zig, and the extracted stock API 30 base (`kernel-ranchu`, `ramdisk.img`). The workspace must be fresh, on a case-sensitive file system (the source has 13 case-collision groups) and its path must not contain spaces. The build uses about 4.3 GiB, plus about 200 MiB for the compiler subset and 170 MiB for the source archive. Kbuild needs `bash`, `perl`, `bc`, `cpio` and POSIX file utilities from the host.

```bash
recipe=profiles/common-qinggan-11/components/kernel
src/prepare/kernel/kernel-tool.sh "$recipe" prepare "$PWD" "$WORKSPACE" "$STOCK_API30_BASE"
src/prepare/kernel/kernel-tool.sh "$recipe" build "$PWD" "$WORKSPACE" 2
src/prepare/kernel/kernel-tool.sh "$recipe" publish "$PWD" "$WORKSPACE"
```

`prepare` refuses existing source and output trees, verifies the stock inputs, applies the netlink reset, host build and OID registry path patches (the last keeps the absolute script path out of a generated source), installs the driver and reconstructs the 4,977-symbol KMI list from 18 official lists. `build` installs missing host tools and compiler blobs, limits make and LLD to 1 to 4 workers and checks the stock security mechanisms after Kconfig. `publish` rejects unlisted config changes, reruns the module ABI guard and writes the content-addressed Image and descriptor. It never edits a profile or claims runtime acceptance.

The `kernels` workflow builds this recipe on Linux, verifies the Image against the profile pin and uploads it as the `Image` asset of release `kernel-qinggan-11-<sha8>`. Profiles pin that URL and SHA-256; `fetch` downloads it. Every host downloads the published Image; a local build only reproduces or updates it.

## Reproducibility and configuration

The build runs with a minimal environment, fixed `KBUILD_BUILD_*` values, `-ffile-prefix-map` for the workspace and the compiler, and the pinned toybox tar and xz for the embedded headers archive. The output directory sits inside the source tree (`out` links to `common/out`) so Kbuild passes relative source paths: ThinLTO names promoted local symbols by a module hash, and absolute paths would reach kallsyms. The KMI list lives in the source tree and the embedded configuration names it relatively. Configuration deviations from the stock embedded config: `CONFIG_CC_CAN_LINK` and `CONFIG_UAPI_HEADER_TEST` are unset because the host has no Linux userland link sysroot, and `CONFIG_UNUSED_KSYMS_WHITELIST` points to the same 4,977 symbols. Protocol 21 requires `CONFIG_CRYPTO_USER=n`, which is already the stock API 30 setting.

Netlink reset purges only kernel-origin datagrams for the socket's multicast group and network namespace and reports `ENETRESET` to receivers. Receiver queue overflow reports `ENOBUFS` through stock netlink unless a receiver suppresses it. Neither a sampled model snapshot nor a built kernel establishes lossless native receive history.

Before admitting a new Image into A11 factory acceptance, check the boot with the unmodified stock modules, enforcing SELinux, `/dev/cis_can_model`, the live CAN daemon and the OEM Binder bridge. Native receiver epochs, loss and overflow errors and terminal watermarks are checked separately. The kernel, daemon and observer of a profile must use the same `can_boundary.h`.
