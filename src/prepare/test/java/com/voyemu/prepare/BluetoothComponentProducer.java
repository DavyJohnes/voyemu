package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

/** Scoped real production component producer; never represents a full profile or ready machine. */
public final class BluetoothComponentProducer {
    static final String PROFILE = "free-09-chinese-20260106-6.0.3";
    static final List<String> COMPONENTS = List.of("bluetooth");

    static void need(boolean value, String message) {
        Failure.require(value, "QINGGAN09_BLUETOOTH_COMPONENT", message, 3);
    }

    static void graph(Map<String, Object> data) {
        var components = Json.obj(data, "components");

        for (String name : COMPONENTS) {
            need(
                    ("components/" + name).equals(components.get(name)),
                    "Exact A9 Bluetooth component source required");
            long count =
                    Json.list(data.get("install")).stream()
                            .map(Json::object)
                            .filter(n ->
                                    ("component:" + name).equals(n.get("artifact"))
                                            && "file".equals(n.get("type"))
                                            && "/system/framework/Ivt8BluetoothAdapter.jar"
                                                    .equals(n.get("target"))
                                            && "0644".equals(n.get("mode")))
                            .count();
            need(count == 1, "Exact normal framework JAR installation required");
        }
    }

    static Path pin(Path root, Path file, String expected, Map<Path, String> tracked)
            throws Exception {
        need(
                file.isAbsolute()
                        && file.normalize().equals(file)
                        && file.startsWith(root.resolve(".local")),
                "Local canonical input required");
        Storage.confined(file, ".");
        NativeCore.pinned(file, expected);
        tracked.put(file, expected);

        return file;
    }

    static Map<String, String> sourcePins(Path recipe) throws Exception {
        var pins = new TreeMap<String, String>();

        for (String component : COMPONENTS) {
            for (var file :
                    FilesSupport.tree(recipe.resolve("components/" + component)).entrySet()) {
                pins.put(
                        "components/" + component + "/" + file.getKey(),
                        FilesSupport.hash(file.getValue()));
            }
        }

        return pins;
    }

    static Map<String, String> freeze(Path root, Path selected, Map<String, String> sources)
            throws Exception {
        var data = Json.object(Json.parse(Files.readString(selected)));
        var pins = new TreeMap<String, String>();
        String prefix = "profiles/" + PROFILE + "/";

        for (var e : data.entrySet()) {
            if (e.getKey().startsWith(prefix + "components/bluetooth/")) {
                need(
                        e.getValue() instanceof String
                                && e.getValue()
                                        .equals(sources.get(e.getKey().substring(prefix.length()))),
                        "Bluetooth source freeze differs");
                pins.put(e.getKey(), (String) e.getValue());
            }
        }

        need(pins.size() == sources.size(), "Complete reviewed Bluetooth source freeze required");

        return pins;
    }

    static Path signingFile(
            Path root, Map<String, Object> signing, String name, Map<Path, String> tracked)
            throws Exception {
        return pin(
                root,
                Path.of(Json.str(Json.obj(signing, name), "file")),
                Json.str(Json.obj(signing, name), "sha256"),
                tracked);
    }

    static Path donorFile(
            Path root, Storage storage, ProfileRegistry.Profile profile, Map<Path, String> tracked)
            throws Exception {
        var donor = Json.object(Json.obj(profile.data(), "original").get("VehicleSetting"));

        return pin(
                root,
                Storage.confined(storage.originals(PROFILE), Json.str(donor, "file")),
                Json.str(donor, "sha256"),
                tracked);
    }

    static String platformCertificatePin(Path cert) throws Exception {
        String certificatePin =
                NativeCore.hash(
                        java.security.cert.CertificateFactory.getInstance("X.509")
                                .generateCertificate(
                                        new java.io.ByteArrayInputStream(Files.readAllBytes(cert)))
                                .getEncoded());

        need(
                certificatePin.equals(
                        "c8a2e9bccf597c2fb6dc66bee293fc13f2fc47ec77bc6b2b0d52c11f51192ab8"),
                "Exact A9 base platform signer required");

        return certificatePin;
    }

    static Map<String, Object> componentArtifact(Artifacts builder, String name) throws Exception {
        Path jar = builder.resolve("component:" + name);
        var entries = NativeCore.entries(jar);

        need(entries.keySet().equals(Set.of("classes.dex")), "DEX-only framework JAR required");
        var classes = new TreeSet<>(NativeCore.classes(DexOperations.dex(jar, "classes.dex", 28)));
        need(
                !classes.isEmpty()
                        && classes.stream()
                                .allMatch(
                                        n -> n.startsWith("Lcom/qinggan/bluetooth/internal/ivt8/")),
                "Own adapter namespace only; OEM ABI must remain external");
        need(
                classes.contains("Lcom/qinggan/bluetooth/internal/ivt8/Ivt8BTAdapter;")
                        && classes.contains(
                                "Lcom/qinggan/bluetooth/internal/ivt8/SyntheticPhonePeer;"),
                "Both own adapter classes required");

        return Map.of(
                "file",
                jar.toString(),
                "sha256",
                FilesSupport.hash(jar),
                "bytes",
                Files.size(jar),
                "target",
                "/system/framework/Ivt8BluetoothAdapter.jar",
                "api",
                28,
                "classes",
                classes,
                "compile_only_classes_packaged",
                false);
    }

    public static void main(String[] args) throws Exception {
        need(
                args.length == 4,
                "Arguments: canonical root, fresh local output, existing production tool request,"
                        + " reviewed source freeze");
        Path root = Path.of(args[0]).toRealPath(),
                stage = Path.of(args[1]).toAbsolutePath().normalize(),
                context = Path.of(args[2]).toRealPath(),
                freezeFile = Path.of(args[3]).toRealPath();
        need(
                stage.startsWith(root.resolve(".local/build/tmp"))
                        && !Files.exists(stage, LinkOption.NOFOLLOW_LINKS),
                "Fresh private output required");
        var registry = new ProfileRegistry(root, PROFILE);
        var profile = registry.get(PROFILE);
        graph(profile.data());
        Path recipe = root.resolve("profiles/" + PROFILE);
        var sources = sourcePins(recipe);
        var reviewed = freeze(root, freezeFile, sources);
        String profilePin = FilesSupport.hash(recipe.resolve("profile.json")),
                contextPin = FilesSupport.hash(context),
                freezePin = FilesSupport.hash(freezeFile);
        var originalRequest = Json.object(Json.parse(Files.readString(context)));
        need(
                PROFILE.equals(originalRequest.get("profile"))
                        && Json.integer(originalRequest, "schema") == 1,
                "Actual A9 preparation tool context required");
        var contextTools = Json.obj(originalRequest, "tools");
        Map<Path, String> tracked = new LinkedHashMap<>();
        Path local = root.resolve(".local");
        var storage =
                new Storage(
                        local,
                        local.resolve("config"),
                        local.resolve("cache"),
                        local.resolve("state"),
                        local.resolve("run"),
                        local.resolve("logs"));
        Path toolManifest = root.resolve("src/app/toolchain.json");
        need(
                FilesSupport.hash(toolManifest).equals(Json.str(contextTools, "manifest_sha256")),
                "Production toolchain source pin differs");
        var toolchain = Json.object(Json.parse(Files.readString(toolManifest)));
        var toolchainPin = FilesSupport.hash(toolManifest);
        Path sdk = Path.of(Json.str(contextTools, "sdk"));

        for (var e : Json.obj(contextTools, "files").entrySet()) {
            pin(root, Storage.confined(sdk, e.getKey()), (String) e.getValue(), tracked);
        }

        var platform = Json.obj(contextTools, "platform");
        Path platformFile =
                pin(
                        root,
                        Path.of(Json.str(platform, "file")),
                        Json.str(platform, "sha256"),
                        tracked);
        var fetched =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        local.resolve(
                                                "inputs/" + PROFILE + "/native-inputs.json"))));
        var signing = Json.obj(fetched, "signing");
        var requested = Json.obj(contextTools, "signing");
        Path key = signingFile(root, signing, "key", tracked);
        Path cert = signingFile(root, signing, "cert", tracked);
        need(
                key.toString().equals(requested.get("key"))
                        && cert.toString().equals(requested.get("certificate"))
                        && tracked.get(key).equals(requested.get("key_sha256"))
                        && tracked.get(cert).equals(requested.get("certificate_sha256")),
                "Production signing context differs");
        Path vehicle = donorFile(root, storage, profile, tracked);
        String certificatePin = platformCertificatePin(cert);
        var tools = new Toolchains(storage, toolchain, sdk, key, cert, platformFile);
        need(
                tracked.containsKey(tools.buildTool("aapt2")),
                "Pinned actual resource linker required");
        registry.verifySource(profile);
        Files.createDirectory(stage);
        var builder =
                new Artifacts(
                        profile,
                        storage,
                        tools,
                        registry.sourceDirectory(profile),
                        stage.resolve("work"),
                        p -> {
                            throw new AssertionError(
                                    "Own two-component build requested Android image input: " + p);
                        },
                        System.err::println);
        var artifacts = new TreeMap<String, Object>();

        for (String name : COMPONENTS) {
            artifacts.put("component:" + name, componentArtifact(builder, name));
        }

        registry.verifySource(profile);
        need(
                profilePin.equals(FilesSupport.hash(recipe.resolve("profile.json")))
                        && sources.equals(sourcePins(recipe))
                        && reviewed.equals(freeze(root, freezeFile, sources)),
                "Bluetooth component source changed during preparation");
        need(
                contextPin.equals(FilesSupport.hash(context))
                        && freezePin.equals(FilesSupport.hash(freezeFile))
                        && toolchainPin.equals(FilesSupport.hash(toolManifest)),
                "Scoped preparation context changed");

        for (var e : tracked.entrySet()) {
            need(
                    e.getValue().equals(FilesSupport.hash(e.getKey())),
                    "Pinned production input changed");
        }

        var result = new LinkedHashMap<String, Object>();
        result.put("schema", 1);
        result.put("kind", "qinggan09-bluetooth-component-refresh");
        result.put("profile", PROFILE);
        result.put("profile_sha256", profilePin);
        result.put("revision", profile.revision());
        result.put("source_pins", sources);
        result.put(
                "reviewed_source_freeze",
                Map.of(
                        "file",
                        freezeFile.toString(),
                        "sha256",
                        freezePin,
                        "relevant_sources",
                        reviewed));
        result.put(
                "production_tool_context",
                Map.of("file", context.toString(), "sha256", contextPin));
        result.put("donor", Map.of("file", vehicle.toString(), "sha256", tracked.get(vehicle)));
        result.put("artifacts", artifacts);
        result.put(
                "signing",
                Map.of(
                        "certificate_sha256",
                        certificatePin,
                        "certificate_file_sha256",
                        tracked.get(cert)));
        result.put("complete_profile_artifacts", false);
        result.put("machine_ready", false);
        result.put("guest_started", false);
        result.put("machine_published", false);
        Files.writeString(
                stage.resolve("receipt.json"),
                Json.write(result) + "\n",
                StandardOpenOption.CREATE_NEW);
        System.out.println(Json.write(result));
    }
}
