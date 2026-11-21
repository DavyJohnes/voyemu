# Tests

All tests run through CTest from a build tree configured with `BUILD_TESTING=ON`, see [build](build.md). Tests check behavior: they run code and compare results, and never read source files or pin repository files by hash; see [tests check behavior](../why/behavior-tests.md).

```sh
ctest --test-dir .local/build/native --output-on-failure --parallel 1
```

The Windows startup fence test needs KornShell on POSIX hosts. It uses `/bin/ksh` by default; set `VOYEMU_TEST_KSH` to an existing KornShell executable to use a user installation. An invalid explicit path fails the test; a missing default shell skips its fence execution. Linux CI extracts the pinned Ubuntu KornShell package into `.local/toolchains` and sets this variable for CTest.

## C and C++ tests

A C++ test lives next to the code it tests as `<name>.test.cpp`, for example `src/app/api/api.test.cpp` beside `api.cpp`. CMake finds exactly one adjacent test source for each registered name and fails configuration otherwise. Tests of guest C code live next to it in `src/guest/qnx/` as `<name>.test.c`. Doubles of guest interfaces, such as `guest_android_fake.hpp`, stay beside the interface they replace.

Test executables are built in the same Release configuration as the product, but with assertions enabled: every target whose name ends in `_test` is compiled without `NDEBUG`, so `assert` checks run.

## Registration and labels

Every test is registered with `voyemu_add_test` in `src/app/CMakeLists.txt`; labels and a timeout in seconds are mandatory, and configuration fails for a test registered otherwise.

```cmake
voyemu_add_test(update_image LABELS deps TIMEOUT 60 COMMAND update_image_test "${voyemu_project_root}")
```

| Label | The test needs |
| --- | --- |
| `host` | Only the build tree and the pinned build tools, including the JDK |
| `deps` | Toolchains that `deps` installs, such as the Android SDK, Java libraries or the QNX update codec |
| `oem` | Imported OEM inputs |
| `source-fixture` | Together with `deps`: only public pinned tools, so the `source-fixtures` job of [CI](ci.md) runs it |

Select tests by label with `ctest --label-regex` and `--label-exclude`; [CI](ci.md) runs the tests without the `oem` and `deps` labels in the Linux, macOS and Windows native build jobs (the host-only layer):

```sh
ctest --test-dir .local/build/linux --label-exclude 'oem|deps' --output-on-failure
```

## Java tests

The `source-fixtures` job in [CI](ci.md) runs the public Java behavior suites and native component handoff on Linux, macOS and Windows. The pinned toolchain action installs Java libraries, Android 34, Build Tools 36.1.0, NDK 29 and its CMake/Ninja from repository URL/SHA-256 pins; the handoff builds the native components of Free 6.0.3 from their recipe with that NDK while their release assets are not published. This test setup downloads tools separately from the `fetch` command that imports OEM inputs; it does not install an emulator or build QEMU. The job selects the `source-fixture` label, excludes `oem` explicitly and requires every selected suite to run successfully without skips. Guest Java compiles with `--release 8` on the same pinned JDK 25 used for host tests.

`passion_carsettings_source`, `canbus_source` and `prepare_java_android_stubs_source` explicitly select repository-owned fixtures with `TEST_OEM=OFF`. Their separate `_oem` suites select `TEST_OEM=ON` and require the imported Passion CarSettings/Multimedia/Camera APKs, the Free 7.3.1 QGAPI/Launcher originals or the CanBus donor APKs of Free 6.0.3, 6.11.1 and 7.3.1 used by their assertions. Missing OEM inputs fail those suites; they are excluded from the public job by the `oem` label.

Java tests live under `src/prepare/test/`:

| Directory | Content |
| --- | --- |
| `java/com/voyemu/prepare/` | Tests of single classes of the preparation tools |
| `java/host-unit/` | The CTest runner for source-only Java classes |
| `java/android-stubs/` | Tests of guest Java code on JVM doubles of Android |
| `java/oem/` | Recipe tests per profile group that need OEM inputs |
| `java/guest-units/` | The CTest runner for the tests of `src/guest/android/api/` and of guest components with JVM doubles of Android in `host-tests/` |
| Other directories | Scenario tests of one recipe or component, each with its own `run.cmake` or `run.sh` |

Java tests run on the pinned JDK of the host; `VOYEMU_JDK` selects another JDK home.

## Tests that need inputs

Some legacy tests that need OEM inputs, a real ANGLE build or other pinned inputs exit with code `77` when those inputs are absent, and CTest reports them as skipped. Strict phone, native handoff and Free 7.3.1 Settings runners instead fail on missing prerequisites; selection by label does not change their behavior. Legacy CMake test scripts that skip exit with `cmake_language(EXIT 77)`. A skipped test is not a pass: the report of a check run names every skip and its reason. Host tests do not replace acceptance of a guest scenario on a running machine.

## Temporary files

Tests write temporary files under `.local/build/tmp/` or the build tree, never into the source tree.
