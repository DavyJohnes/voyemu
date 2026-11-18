package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.UUID;

final class Toolchains {
    record Component(String target, Map<String, Object> pin, String revision, List<String> files) {}

    private final Storage storage;
    final Map<String, Object> manifest;
    final Path sdk;
    final String host;
    final boolean windows;
    private final Path explicitKey, explicitCertificate, explicitPlatform;

    Toolchains(Storage storage) throws Exception {
        this(
                storage,
                Json.object(Json.parse(ProfileRegistry.text("voyemu/toolchain.json"))),
                null,
                null,
                null);
    }

    Toolchains(
            Storage storage,
            Map<String, Object> selectedManifest,
            Path selectedSdk,
            Path selectedKey,
            Path selectedCertificate)
            throws Exception {
        this(storage, selectedManifest, selectedSdk, selectedKey, selectedCertificate, null);
    }

    Toolchains(
            Storage storage,
            Map<String, Object> selectedManifest,
            Path selectedSdk,
            Path selectedKey,
            Path selectedCertificate,
            Path selectedPlatform)
            throws Exception {
        this.storage = storage;
        manifest = Map.copyOf(selectedManifest);
        explicitKey = selectedKey;
        explicitCertificate = selectedCertificate;
        explicitPlatform = selectedPlatform;
        String os = System.getProperty("os.name", "").toLowerCase(java.util.Locale.ROOT);
        host =
                os.contains("mac") || os.contains("darwin")
                        ? "darwin"
                        : os.contains("windows") ? "win32" : "linux";
        windows = host.equals("win32");
        String override = System.getenv("VOYEMU_ANDROID_SDK");
        sdk =
                selectedSdk != null
                        ? selectedSdk
                        : override != null
                                ? Path.of(override).toAbsolutePath().normalize()
                                : storage.sdk();
    }

    Path adb() {
        return sdk.resolve("platform-tools/adb" + (windows ? ".exe" : ""));
    }

    Path emulator() {
        return sdk.resolve("emulator/emulator" + (windows ? ".exe" : ""));
    }

    Path platform() {
        if (explicitPlatform != null) {
            return explicitPlatform;
        }

        return sdk.resolve(
                "platforms/android-"
                        + Json.integer(Json.obj(manifest, "sdk"), "compileSdk")
                        + "/android.jar");
    }

    Path buildTool(String name) {
        return sdk.resolve(
                "build-tools/"
                        + Json.str(Json.obj(manifest, "sdk"), "buildTools")
                        + "/"
                        + name
                        + (windows ? ".exe" : ""));
    }

    Path key(String kind) {
        if (kind.equals("key") && explicitKey != null) {
            return explicitKey;
        }

        if (kind.equals("cert") && explicitCertificate != null) {
            return explicitCertificate;
        }

        return storage.data()
                .resolve("toolchains/keys/platform." + (kind.equals("key") ? "pk8" : "x509.pem"));
    }

    List<Component> components() {
        Failure.require(
                !host.equals("linux"), "PLATFORM_UNSUPPORTED", "Linux runtime is not supported", 3);
        Map<String, Object> sdkSpec = Json.obj(manifest, "sdk");
        Map<String, Object> pins = Json.obj(Json.obj(sdkSpec, "archives"), host);
        Map<String, Object> emulatorPin;

        if (host.equals("darwin") && pins.containsKey("emulatorByArch")) {
            String arch = System.getProperty("os.arch").equals("aarch64") ? "arm64" : "x64";
            emulatorPin = Json.obj(Json.obj(pins, "emulatorByArch"), arch);
        } else {
            emulatorPin = Json.obj(pins, "emulator");
        }

        Map<String, Object> nativeSpec = Json.obj(manifest, "native");
        String exe = windows ? ".exe" : "";
        List<Component> result = new ArrayList<>();
        result.add(
                new Component(
                        "platform-tools",
                        Json.obj(pins, "platformTools"),
                        Json.str(sdkSpec, "platformTools"),
                        List.of("adb" + exe)));
        result.add(
                new Component(
                        "emulator",
                        emulatorPin,
                        Json.str(sdkSpec, "emulator"),
                        List.of("emulator" + exe)));
        result.add(
                new Component(
                        "build-tools/" + Json.str(sdkSpec, "buildTools"),
                        Json.obj(pins, "buildTools"),
                        Json.str(sdkSpec, "buildTools"),
                        List.of("aapt2" + exe, "zipalign" + exe)));
        result.add(
                new Component(
                        "platforms/android-" + Json.integer(sdkSpec, "compileSdk"),
                        Json.obj(pins, "platform"),
                        Json.str(sdkSpec, "platformRevision"),
                        List.of("android.jar")));
        result.add(
                new Component(
                        "cmake/" + Json.str(nativeSpec, "cmake"),
                        Json.obj(pins, "cmake"),
                        Json.str(nativeSpec, "cmake"),
                        List.of("bin/cmake" + exe, "bin/ninja" + exe)));

        return result;
    }

    Map<String, Object> check() throws Exception {
        List<Object> checks = new ArrayList<>();
        boolean ok = true;

        for (Component component : components()) {
            boolean valid = valid(sdk.resolve(component.target()), component);
            ok &= valid;
            checks.add(
                    Map.of(
                            "id",
                            component.target(),
                            "status",
                            valid ? "pass" : "fail",
                            "version",
                            component.revision()));
        }

        return Map.of("ok", ok, "sdk", sdk.toString(), "checks", checks);
    }

    void deps(java.util.function.Consumer<String> log) throws Exception {
        List<Component> components = components();
        boolean complete = true;

        for (Component component : components) {
            complete &= valid(sdk.resolve(component.target()), component);
        }

        if (!complete) {
            Failure.require(
                    System.getenv("VOYEMU_ANDROID_SDK") == null,
                    "SDK_METADATA_MISMATCH",
                    "Explicit SDK is incomplete; it will not be modified",
                    3);
            Path parent = sdk.getParent();
            Storage.directory(parent);
            Path stage = parent.resolve("sdk.stage-" + UUID.randomUUID());
            Storage.directory(stage);

            try {
                for (Component component : components) {
                    log.accept("Preparing pinned " + component.target());
                    Path archive =
                            Downloads.cached(
                                    component.pin(),
                                    storage.cache().resolve("sdk"),
                                    3L * 1024 * 1024 * 1024,
                                    false);
                    Path unpacked = stage.resolve("unpacked/" + component.target());
                    Storage.directory(unpacked.getParent());
                    Archives.extract(archive, unpacked, 8L * 1024 * 1024 * 1024, true);
                    collapse(unpacked);
                    Failure.require(
                            valid(unpacked, component),
                            "SDK_METADATA_MISMATCH",
                            "SDK component differs from the pinned layout: " + component.target(),
                            3);
                    Path target = stage.resolve(component.target());
                    Storage.directory(target.getParent());
                    Files.move(unpacked, target, StandardCopyOption.ATOMIC_MOVE);
                }

                FilesSupport.removeOwnedTree(stage.resolve("unpacked"));
                Path old = parent.resolve("sdk.previous-" + UUID.randomUUID());

                if (Files.exists(sdk)) {
                    Files.move(sdk, old, StandardCopyOption.ATOMIC_MOVE);
                }

                try {
                    Files.move(stage, sdk, StandardCopyOption.ATOMIC_MOVE);
                } catch (Exception failure) {
                    // checkstyle-allow IllegalCatch: rollback, then rethrow unchanged
                    if (Files.exists(old)) {
                        Files.move(old, sdk, StandardCopyOption.ATOMIC_MOVE);
                    }

                    throw failure;
                }
            } finally {
                if (Files.exists(stage)) {
                    FilesSupport.removeOwnedTree(stage);
                }
            }
        }

        Map<String, Object> platform = Json.obj(Json.obj(manifest, "signing"), "platform");

        for (String kind : List.of("key", "cert")) {
            Map<String, Object> pin = Json.obj(platform, kind);
            Path fetched =
                    Downloads.cached(pin, storage.cache().resolve("sources"), 1024 * 1024, true);
            Path destination = key(kind);
            Storage.directory(destination.getParent());

            if (Files.exists(destination)) {
                Failure.require(
                        FilesSupport.hash(destination).equals(Json.str(pin, "sha256")),
                        "SIGNING_INPUT_INVALID",
                        "Existing signing input differs",
                        3);
            } else {
                Files.copy(fetched, destination);
            }
        }

        Storage.atomic(
                storage.data().resolve("toolchains/licenses.json"),
                Json.write(
                                Map.of(
                                        "schema",
                                        1,
                                        "androidSdk",
                                        "https://developer.android.com/studio/terms",
                                        "acceptedBy",
                                        "voyemu deps"))
                        + "\n");
    }

    private static boolean valid(Path directory, Component component) throws Exception {
        if (!Files.isDirectory(directory) || Files.isSymbolicLink(directory)) {
            return false;
        }

        Path source = directory.resolve("source.properties");

        if (!Files.isRegularFile(source)) {
            return false;
        }

        Properties props = new Properties();

        try (var input = Files.newInputStream(source)) {
            props.load(input);
        }

        if (!component.revision().equals(props.getProperty("Pkg.Revision"))) {
            return false;
        }

        for (String file : component.files()) {
            if (!Files.isRegularFile(directory.resolve(file))) {
                return false;
            }
        }

        return true;
    }

    private static void collapse(Path directory) throws Exception {
        while (true) {
            List<Path> paths;

            try (var stream = Files.list(directory)) {
                paths = stream.toList();
            }

            if (paths.size() != 1 || !Files.isDirectory(paths.get(0))) {
                return;
            }

            Path wrapper = paths.get(0);
            Path aside =
                    directory.resolveSibling(
                            directory.getFileName() + ".aside-" + UUID.randomUUID());
            Files.move(wrapper, aside, StandardCopyOption.ATOMIC_MOVE);

            try (var stream = Files.list(aside)) {
                for (Path file : stream.toList()) {
                    Files.move(
                            file,
                            directory.resolve(file.getFileName()),
                            StandardCopyOption.ATOMIC_MOVE);
                }
            }

            Files.delete(aside);
        }
    }
}
