package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Scoped real production component producer; never represents a full profile or ready machine. */
public final class OwnAudioComponentsProducer {
    static final String PROFILE = "free-09-chinese-20260106-6.0.3";
    static final List<String> COMPONENTS = List.of("audiopolicy", "bootstrap");
    static final List<String> CANBUS_COMPONENTS = List.of("audiopolicy", "bootstrap", "canbus");
    static final Map<String, String> PACKAGES =
            Map.of(
                    "audiopolicy",
                    "com.qinggan.audiopolicy.service",
                    "bootstrap",
                    "com.voyemu",
                    "canbus",
                    "com.qinggan.canbus.service");

    static List<String> components(String[] args) {
        need(
                args.length == 4 || (args.length == 5 && "--canbus".equals(args[4])),
                "Arguments: canonical root, fresh local output, existing production tool request,"
                        + " reviewed source freeze, optional --canbus");

        return args.length == 4 ? COMPONENTS : CANBUS_COMPONENTS;
    }

    static void need(boolean value, String message) {
        Failure.require(value, "OWN_AUDIO_COMPONENTS", message, 3);
    }

    static void graph(Map<String, Object> data) {
        graph(data, COMPONENTS);
    }

    static void graph(Map<String, Object> data, List<String> selected) {
        var components = Json.obj(data, "components");

        for (String name : selected) {
            need(
                    ("components/" + name).equals(components.get(name)),
                    "Exact A9 own component source required");
            long count =
                    Json.list(data.get("install")).stream()
                            .map(Json::object)
                            .filter(n ->
                                    ("component:" + name).equals(n.get("artifact"))
                                            && "apk".equals(n.get("type"))
                                            && (name.equals("canbus")
                                                    ? "/system/priv-app/VoyemuCanBus/VoyemuCanBus.apk"
                                                                    .equals(n.get("target"))
                                                            && "com.qinggan.canbus.service"
                                                                    .equals(n.get("package"))
                                                    : !n.containsKey("target")))
                            .count();
            need(count == 1, "Own component must have exactly one canonical APK installation");
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
        return sourcePins(recipe, COMPONENTS);
    }

    static Map<String, String> sourcePins(Path recipe, List<String> selected) throws Exception {
        var pins = new TreeMap<String, String>();

        for (String component : selected) {
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
        return freeze(root, selected, sources, COMPONENTS);
    }

    static Map<String, String> freeze(
            Path root, Path selected, Map<String, String> sources, List<String> components)
            throws Exception {
        var data = Json.object(Json.parse(Files.readString(selected)));
        var pins = new TreeMap<String, String>();
        String prefix = "profiles/" + PROFILE + "/";

        for (var e : data.entrySet()) {
            if (components.stream()
                    .anyMatch(c -> e.getKey().startsWith(prefix + "components/" + c + "/"))) {
                need(
                        e.getValue() instanceof String
                                && e.getValue()
                                        .equals(sources.get(e.getKey().substring(prefix.length()))),
                        "Audio source freeze differs");
                pins.put(e.getKey(), (String) e.getValue());
            }
        }

        need(
                pins.keySet().stream().anyMatch(p -> p.contains("/audiopolicy/"))
                        && pins.keySet().stream().anyMatch(p -> p.contains("/bootstrap/")),
                "Both reviewed own component source freezes required");

        if (components.equals(CANBUS_COMPONENTS)) {
            var expected = new TreeMap<String, String>();

            for (var e : sources.entrySet()) {
                expected.put(prefix + e.getKey(), e.getValue());
            }

            need(pins.equals(expected), "Complete reviewed three-component source freeze required");
        }

        return pins;
    }

    static Storage localStorage(Path local) {
        return new Storage(
                local,
                local.resolve("config"),
                local.resolve("cache"),
                local.resolve("state"),
                local.resolve("run"),
                local.resolve("logs"));
    }

    static Map<String, Object> fetchedSigning(Path local) throws Exception {
        var fetched =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        local.resolve(
                                                "inputs/" + PROFILE + "/native-inputs.json"))));

        return Json.obj(fetched, "signing");
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

    static Map<String, Object> componentArtifact(
            Artifacts builder, Toolchains tools, Path stage, String name, String certificatePin)
            throws Exception {
        Path apk = builder.resolve("component:" + name);
        String badging =
                Processes.run(
                                tools.buildTool("aapt2"),
                                List.of("dump", "badging", apk.toString()),
                                stage,
                                Duration.ofSeconds(30),
                                false)
                        .stdout();

        need(
                badging.contains("package: name='" + PACKAGES.get(name) + "'")
                        && badging.contains("sdkVersion:'28'"),
                "Prepared own APK package or minimum SDK differs");
        var signature =
                new com.android.apksig.ApkVerifier.Builder(apk.toFile())
                        .setMinCheckedPlatformVersion(28)
                        .setMaxCheckedPlatformVersion(28)
                        .build()
                        .verify();
        need(
                signature.isVerified()
                        && signature.getSignerCertificates().size() == 1
                        && certificatePin.equals(
                                NativeCore.hash(
                                        signature.getSignerCertificates().get(0).getEncoded())),
                "Actual API28 signature differs");

        return Map.of(
                "file",
                apk.toString(),
                "sha256",
                FilesSupport.hash(apk),
                "bytes",
                Files.size(apk),
                "package",
                PACKAGES.get(name),
                "certificate_sha256",
                certificatePin,
                "signature_verified_api",
                28,
                "signer_count",
                1,
                "min_sdk",
                28,
                "package_verified",
                true);
    }

    public static void main(String[] args) throws Exception {
        var selected = components(args);
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
        graph(profile.data(), selected);
        Path recipe = root.resolve("profiles/" + PROFILE);
        var sources = sourcePins(recipe, selected);
        var reviewed = freeze(root, freezeFile, sources, selected);
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
        var storage = localStorage(local);
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
        var signing = fetchedSigning(local);
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
                                    "Own component build requested Android image input: " + p);
                        },
                        System.err::println);
        var artifacts = new TreeMap<String, Object>();

        for (String name : selected) {
            artifacts.put(
                    "component:" + name,
                    componentArtifact(builder, tools, stage, name, certificatePin));
        }

        registry.verifySource(profile);
        need(
                profilePin.equals(FilesSupport.hash(recipe.resolve("profile.json")))
                        && sources.equals(sourcePins(recipe, selected))
                        && reviewed.equals(freeze(root, freezeFile, sources, selected)),
                "Own component source changed during preparation");
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
        result.put("kind", "own-component-refresh");
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
