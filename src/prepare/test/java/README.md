# Standalone host checks

These checks run from the repository root with JDK 25. CTest `prepare_java_host_unit` runs them with the other source-only classes listed in [host-unit/run.cmake](host-unit/run.cmake), except `PassionMcuPropertyHostTest`, which needs an OEM APK. For a manual run, compile the launchers against the existing JSON utility into an owned job directory. `--release 25` matches the class-file level of the CTest and Java helper builds.

```sh
classes=.local/build/jobs/java-host-checks/classes
tests=src/prepare/test/java/com/voyemu/prepare
mkdir -p "$classes"
javac --release 25 -d "$classes" src/prepare/java/com/voyemu/prepare/Json.java \
  "$tests/HostTestRunner.java" \
  "$tests/FreeStartupAudioHostTest.java" \
  "$tests/NetworkSetupReadinessTest.java" "$tests/HiboardTopGestureHostTest.java" \
  "$tests/PassionMcuPropertyHostTest.java"
```

Use a fresh `WORK` path for each run. `JDK` is the JDK home containing `release` and `bin`.

| Class in `com.voyemu.prepare` | Arguments after `java -cp "$classes" CLASS` |
| --- | --- |
| `FreeStartupAudioHostTest` | `ROOT WORK JDK` |
| `NetworkSetupReadinessTest` | None from the repository root, or `--root ROOT` |
| `HiboardTopGestureHostTest` | `--root ROOT --work WORK --jdk JDK` |
| `PassionMcuPropertyHostTest` | `--root ROOT --work WORK --toolchain TOOLCHAIN --platform ANDROID_JAR --original OEM_APK` |

CTest `display_routing` runs the Free A11 7.3.1 display router checks; a manual run uses `java src/prepare/test/java/display-routing/Run.java <hiboard|statusbar> ROOT WORK JDK`. See the [phone branch check](../phone-window-branch/README.md) for the OEM smali cases.
