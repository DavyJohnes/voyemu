# Java preparation

## Decision

APK, JAR and DEX preparation and input acquisition through `capture.json` run in Java from `src/prepare/`. The user installs no JDK: CI builds and publishes a minimal Java runtime of each host, the Java helper and the guest Java code of the profiles, and [`deps`](../how/preparation.md#deps) installs them. Host-side preparation code compiles with `--release 25`; code that runs inside Android guests compiles with `--release 8` against the Android classpath. The native CLI keeps archive import and everything else.

## Context

Android adaptations decode, patch and re-encode DEX code and sign APKs. The tools for this are Java libraries: D8 and apksigner from the Android build tools and dexlib2. Their versions are pinned in `src/prepare/libraries.json` and `src/prepare/toolchain.json`. Preparation used to compile its helper and the guest Java code on an installed JDK 25, which every user had to install.

## Alternatives

**A jlink runtime with Java compiled in CI.** jlink links only the modules that the helper uses, `java.base`, `java.logging`, `java.net.http` and `jdk.unsupported`, from the jmods of the pinned JDK: an archive of 15 to 19 MB per host instead of a JDK of about 300 MB. One Linux runner links all three hosts, and two runs give the same bytes. The helper keeps the JVM it was tested on, so its outputs stay the same bytes. Only javac is missing, so the helper and every guest Java source compile in CI.

**GraalVM native-image of the helper.** A native helper produced the same DEX, APK and JAR bytes, but it needs reflection metadata for dexlib2. It is only as fast as the JVM with `-O3` for the build CPU, while a portable x64 build without the SHA instructions hashed about twice as slowly. The image is about 30 MB compressed per host, each host needs its own runner, and two builds of the same input differed, so its pin could only name one CI output.

**Reimplementing DEX and APK handling in C++.** The DEX layer, a writer with the byte output of dexlib2, the recipes and their tests amount to several times the work of either Java route, with the risk of changing outputs with which machines passed acceptance.

**Shell pipelines around SDK command-line tools.** Per-host scripts that parse tool output and start a process for every step.

## Argument

Calling the Java libraries in process gives typed access to DEX structures for `guard` checks and recipes, without parsing text output. A jlink runtime keeps that code and its outputs unchanged, is reproducible like the other CI assets and adds no compiler or metadata to maintain. Guest code at `--release 8` stays within the Java APIs of the oldest Android guests.

## Consequences

Preparing and launching a machine need no JDK. `java-runtime`, `java-helper` and `android-java` in [CI](../dev/ci.md) build the three assets, and a change of a helper or guest source needs a new asset; developers select `--jdk` explicitly to build from source before publishing an updated asset. Without that option, preparation uses the pinned helper asset even in a source checkout and reports `JAVA_HELPER_ASSET_MISSING` if it is unavailable. Developers keep a JDK 25 for tests, checks and recipes. Java code passes google-java-format, Checkstyle and Error Prone checks. Libraries and tools follow the [pin rule](pins.md). Java also covers developer checks, which keeps the repository [free of Python](no-python.md). [Components and recipes](../how/components.md) describes what preparation applies.
