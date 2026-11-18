package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;

import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.SortedMap;
import java.util.TreeMap;

/** One real production recipe target; never a full artifacts or machine receipt. */
public final class PhonePackageProducer {
    static final String PROFILE = "free-09-chinese-20260106-6.0.3",
            PACKAGE = "com.qinggan.bluetoothphone";
    static final String CERT = "c8a2e9bccf597c2fb6dc66bee293fc13f2fc47ec77bc6b2b0d52c11f51192ab8";
    static final List<String> COMPONENTS =
            List.of("qg-runtime", "framework-qgapi", "framework-qgmod", "framework-qgvehicle");
    static final List<String> STEPS =
            List.of("systemservice-framework.json", "add-qg-runtime.json", "phone-provider.json");

    static void need(boolean value, String message) {
        Failure.require(value, "QINGGAN09_PHONE_PACKAGE", message, 3);
    }

    static void graph(Map<String, Object> data) {
        var patch = Json.object(Json.obj(data, "patches").get("BluetoothPhone"));

        need(
                patch.keySet().equals(Set.of("type", "input", "uses", "steps", "sign")),
                "Exact registered Phone recipe fields required");
        need(
                "apk".equals(patch.get("type"))
                        && "original:BluetoothPhone".equals(patch.get("input"))
                        && "platform".equals(patch.get("sign")),
                "Exact production Phone target required");
        need(
                Json.list(patch.get("steps")).equals(STEPS),
                "All normal production Phone steps required");
        need(
                Json.list(patch.get("uses"))
                        .equals(COMPONENTS.stream().map(n -> "component:" + n).toList()),
                "All normal production Phone dependencies required");
        var components = Json.obj(data, "components");

        for (String name : COMPONENTS) {
            need(
                    ("components/" + name).equals(components.get(name)),
                    "Exact dependency source required");
        }

        need(
                Json.list(data.get("install")).stream()
                                .map(Json::object)
                                .filter(n ->
                                        "patched:BluetoothPhone".equals(n.get("artifact"))
                                                && "apk".equals(n.get("type"))
                                                && !n.containsKey("target"))
                                .count()
                        == 1,
                "Exactly one normal Phone installation required");
        need(
                "d5a7417be79e2e4bfd70e643df85ae76e1a8a15332a345f126ccc5781f5ba8a3"
                        .equals(
                                Json.object(Json.obj(data, "original").get("BluetoothPhone"))
                                        .get("sha256")),
                "Exact original Phone required");
    }

    static Map<String, String> sourcePins(Path root) throws Exception {
        Path recipe = root.resolve("profiles/" + PROFILE);
        var pins = new TreeMap<String, String>();

        for (String name : COMPONENTS) {
            for (var e : FilesSupport.tree(recipe.resolve("components/" + name)).entrySet()) {
                pins.put(
                        "profiles/" + PROFILE + "/components/" + name + "/" + e.getKey(),
                        FilesSupport.hash(e.getValue()));
            }
        }

        for (String step : STEPS) {
            pins.put(
                    "profiles/" + PROFILE + "/patches/" + step,
                    FilesSupport.hash(recipe.resolve("patches/" + step)));
        }

        for (var e : FilesSupport.tree(recipe.resolve("patches/phone-provider/src")).entrySet()) {
            pins.put(
                    "profiles/" + PROFILE + "/patches/phone-provider/src/" + e.getKey(),
                    FilesSupport.hash(e.getValue()));
        }

        for (var e : FilesSupport.tree(root.resolve("src/prepare/java")).entrySet()) {
            pins.put("src/prepare/java/" + e.getKey(), FilesSupport.hash(e.getValue()));
        }

        pins.put(
                "src/prepare/test/java/com/voyemu/prepare/PhonePackageProducer.java",
                FilesSupport.hash(
                        root.resolve(
                                "src/prepare/test/java/com/voyemu/prepare/PhonePackageProducer.java")));

        return pins;
    }

    static Map<String, String> freeze(Path selected, Map<String, String> sources) throws Exception {
        var all = Json.object(Json.parse(Files.readString(selected)));
        var pins = new TreeMap<String, String>();
        String prefix = "profiles/" + PROFILE + "/patches/phone-provider/src/";

        for (var e : sources.entrySet()) {
            if (e.getKey().startsWith(prefix)) {
                need(
                        e.getValue().equals(all.get(e.getKey())),
                        "Reviewed provider freeze must cover every current provider source");
                pins.put(e.getKey(), e.getValue());
            }
        }

        need(!pins.isEmpty(), "Reviewed provider source required");

        return pins;
    }

    static Map<String, String> methods(Map<String, byte[]> entries) throws Exception {
        var result = new TreeMap<String, String>();

        for (var e : entries.entrySet()) {
            if (e.getKey().matches("classes[0-9]*\\.dex")) {
                for (var cls :
                        new DexBackedDexFile(Opcodes.forApi(28), e.getValue()).getClasses()) {
                    for (var method : cls.getMethods()) {
                        need(
                                result.put(
                                                method.toString(),
                                                FrameworkDexMaterializer.semantic(method))
                                        == null,
                                "Duplicate method");
                    }
                }
            }
        }

        return result;
    }

    static final String WINDOW_METHOD =
            "Lcom/qinggan/bluetoothphone/logic/manager/DialStatusChangeManager;->updateCallState(Lcom/qinggan/bluetoothphone/bean/HfpEvents$CallStateEvent;)V";
    static final String
            WINDOW_FROM =
                    "Lcom/qinggan/bluetoothphone/logic/manager/DialStatusChangeManager;->closeMinOpenMaxWindow(Z)V",
            WINDOW_TO =
                    "Lcom/qinggan/bluetoothphone/logic/manager/DialStatusChangeManager;->openMaxWindow(Z)V";

    static Map<String, Object> methodPreservation(
            Map<String, String> original, Map<String, String> current) {
        need(original.size() == 51799, "Exact original Phone method inventory required");

        for (var e : original.entrySet()) {
            if (!e.getKey().equals(WINDOW_METHOD)) {
                need(
                        e.getValue().equals(current.get(e.getKey())),
                        "Original OEM method changed: " + e.getKey());
            }
        }

        need(
                original.containsKey(WINDOW_METHOD)
                        && current.containsKey(WINDOW_METHOD)
                        && !original.get(WINDOW_METHOD).equals(current.get(WINDOW_METHOD)),
                "Exactly the approved original OEM method must change");
        var delta =
                Map.<String, Object>of(
                        "method",
                        WINDOW_METHOD,
                        "from",
                        WINDOW_FROM,
                        "to",
                        WINDOW_TO,
                        "changed_instructions",
                        1,
                        "original_semantic_sha256",
                        original.get(WINDOW_METHOD),
                        "prepared_semantic_sha256",
                        current.get(WINDOW_METHOD),
                        "argument_false_preserved",
                        true);

        return new LinkedHashMap<>(
                Map.of(
                        "original_methods",
                        51799,
                        "unchanged_original_methods",
                        51798,
                        "all_original_methods_semantically_preserved",
                        false,
                        "production_recipe_unchanged",
                        true,
                        "approved_oem_method_delta",
                        delta));
    }

    static Map<String, Object> preserve(Map<String, byte[]> before, Map<String, byte[]> after)
            throws Exception {
        var original = methods(before);
        var current = methods(after);
        var preservation = methodPreservation(original, current);

        Api28PhoneCallWindowGuard.verify(before.get("classes.dex"), after.get("classes.dex"));

        for (var e : before.entrySet()) {
            if (e.getKey().matches("classes[0-9]*\\.dex") && !e.getKey().equals("classes.dex")) {
                need(
                        Arrays.equals(e.getValue(), after.get(e.getKey())),
                        "Other original OEM DEX bytes changed: " + e.getKey());
            }
        }

        for (String name : current.keySet()) {
            if (!original.containsKey(name)) {
                need(
                        name.startsWith("Lcom/voyemu/input/PhoneControlProvider;")
                                || name.startsWith("Lcom/voyemu/input/PhoneControlProvider$")
                                || name.startsWith("Lcom/voyemu/input/PhonePeerProvider;")
                                || name.startsWith("Lcom/voyemu/input/PhonePeerProvider$"),
                        "Unexpected added method");
            }
        }

        need(
                current.keySet().stream()
                        .anyMatch(s -> s.startsWith("Lcom/voyemu/input/PhoneControlProvider;")),
                "Own provider absent");
        need(
                current.keySet().stream()
                        .anyMatch(s -> s.startsWith("Lcom/voyemu/input/PhonePeerProvider;")),
                "Own peer provider absent");

        for (var e : before.entrySet()) {
            if (!e.getKey().equals("AndroidManifest.xml")
                    && !e.getKey().matches("classes[0-9]*\\.dex")
                    && !DexOperations.signatureEntry(e.getKey())
                    && !e.getKey().equals("lib/arm64-v8a/libqg_runtime.so")) {
                need(
                        Arrays.equals(e.getValue(), after.get(e.getKey())),
                        "Original non-code entry changed: " + e.getKey());
            }
        }

        for (String name : after.keySet()) {
            if (!before.containsKey(name)) {
                need(
                        name.matches("classes[0-9]*\\.dex")
                                || DexOperations.signatureEntry(name)
                                || name.equals("lib/arm64-v8a/libqg_runtime.so"),
                        "Unexpected APK entry added: " + name);
            }
        }

        need(
                after.containsKey("lib/arm64-v8a/libqg_runtime.so"),
                "Production native runtime absent");

        return preservation;
    }

    static Path pin(Path root, Path file, String expected, Map<Path, String> tracked)
            throws Exception {
        need(
                file.isAbsolute()
                        && file.normalize().equals(file)
                        && file.startsWith(root.resolve(".local")),
                "Canonical local production input required");
        Storage.confined(file, ".");
        NativeCore.pinned(file, expected);
        tracked.put(file, expected);

        return file;
    }

    public static void main(String[] args) throws Exception {
        need(
                args.length == 4,
                "Arguments: canonical root, fresh local output, production tool request, reviewed"
                        + " provider freeze");
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
        var sources = sourcePins(root);
        var reviewed = freeze(freezeFile, sources);
        String profilePin = FilesSupport.hash(recipe.resolve("profile.json")),
                contextPin = FilesSupport.hash(context),
                freezePin = FilesSupport.hash(freezeFile);
        var request = Json.object(Json.parse(Files.readString(context)));
        need(
                PROFILE.equals(request.get("profile")) && Json.integer(request, "schema") == 1,
                "Actual production A9 tool context required");
        var contextTools = Json.obj(request, "tools");
        Map<Path, String> tracked = new LinkedHashMap<>();
        Path local = root.resolve(".local");
        var storage = localStorage(local);
        Path manifest = root.resolve("src/app/toolchain.json");
        String manifestPin = FilesSupport.hash(manifest);
        need(
                manifestPin.equals(contextTools.get("manifest_sha256")),
                "Production toolchain source differs");
        var toolchain = Json.object(Json.parse(Files.readString(manifest)));
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
        var signing =
                Json.obj(
                        Json.object(
                                Json.parse(
                                        Files.readString(
                                                local.resolve(
                                                        "inputs/"
                                                                + PROFILE
                                                                + "/native-inputs.json")))),
                        "signing");
        var requested = Json.obj(contextTools, "signing");
        Path key = signingFile(root, signing, "key", tracked),
                cert = signingFile(root, signing, "cert", tracked);
        need(
                key.toString().equals(requested.get("key"))
                        && cert.toString().equals(requested.get("certificate"))
                        && tracked.get(key).equals(requested.get("key_sha256"))
                        && tracked.get(cert).equals(requested.get("certificate_sha256")),
                "Production signing context differs");
        String certificatePin =
                NativeCore.hash(
                        java.security.cert.CertificateFactory.getInstance("X.509")
                                .generateCertificate(
                                        new java.io.ByteArrayInputStream(Files.readAllBytes(cert)))
                                .getEncoded());
        need(CERT.equals(certificatePin), "Exact A9 platform signer required");
        var originals = originalInputs(root, storage, profile, tracked);
        var tools = new Toolchains(storage, toolchain, sdk, key, cert, platformFile);
        need(tracked.containsKey(tools.buildTool("aapt2")), "Pinned actual AAPT2 required");
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
                                    "Single Phone target requested Android image input: " + p);
                        },
                        System.err::println);
        Path apk = builder.resolve("patched:BluetoothPhone");
        var preservation = checkPreparedEntries(storage, builder, apk);
        int minSdk = checkBadging(tools, stage, apk);
        checkSignature(apk);
        registry.verifySource(profile);
        need(
                profilePin.equals(FilesSupport.hash(recipe.resolve("profile.json")))
                        && sources.equals(sourcePins(root))
                        && reviewed.equals(freeze(freezeFile, sources)),
                "Production sources changed during preparation");
        need(
                contextPin.equals(FilesSupport.hash(context))
                        && freezePin.equals(FilesSupport.hash(freezeFile))
                        && manifestPin.equals(FilesSupport.hash(manifest)),
                "Preparation context changed");

        for (var e : tracked.entrySet()) {
            need(
                    e.getValue().equals(FilesSupport.hash(e.getKey())),
                    "Pinned production input changed");
        }

        var result = new LinkedHashMap<String, Object>();
        result.put("schema", 1);
        result.put("kind", "qinggan09-phone-package-refresh");
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
        result.put("originals", originals);
        result.put("artifact", artifactReceipt(apk, minSdk));
        result.put(
                "signing",
                Map.of("certificate_sha256", CERT, "certificate_file_sha256", tracked.get(cert)));
        preservation.put(
                "aapt_badging_sha256", FilesSupport.hash(stage.resolve("aapt-badging.txt")));
        result.put("preservation", preservation);
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

    static Path signingFile(
            Path root, Map<String, Object> signing, String name, Map<Path, String> tracked)
            throws Exception {
        return pin(
                root,
                Path.of(Json.str(Json.obj(signing, name), "file")),
                Json.str(Json.obj(signing, name), "sha256"),
                tracked);
    }

    static Storage localStorage(Path local) {
        var storage =
                new Storage(
                        local,
                        local.resolve("config"),
                        local.resolve("cache"),
                        local.resolve("state"),
                        local.resolve("run"),
                        local.resolve("logs"));

        return storage;
    }

    static SortedMap<String, Object> originalInputs(
            Path root, Storage storage, ProfileRegistry.Profile profile, Map<Path, String> tracked)
            throws Exception {
        var originals = new TreeMap<String, Object>();

        for (String name : List.of("BluetoothPhone", "OEMFramework")) {
            var definition = Json.object(Json.obj(profile.data(), "original").get(name));
            Path file = Storage.confined(storage.originals(PROFILE), Json.str(definition, "file"));

            if (name.equals("BluetoothPhone")) {
                pin(root, file, Json.str(definition, "sha256"), tracked);
                originals.put(name, Map.of("file", file.toString(), "sha256", tracked.get(file)));
            } else {
                var actual = new TreeMap<String, String>();

                for (var e : FilesSupport.tree(file).entrySet()) {
                    actual.put(e.getKey(), FilesSupport.hash(e.getValue()));
                }

                need(
                        actual.equals(Json.obj(definition, "sha256")),
                        "Exact OEM framework input tree required");

                for (var e : actual.entrySet()) {
                    pin(root, file.resolve(e.getKey()), e.getValue(), tracked);
                }

                originals.put(name, Map.of("file", file.toString(), "sha256", actual));
            }
        }

        return originals;
    }

    static Map<String, Object> checkPreparedEntries(Storage storage, Artifacts builder, Path apk)
            throws Exception {
        var originalEntries =
                NativeCore.entries(storage.originals(PROFILE).resolve("BluetoothPhone.apk"));
        var preparedEntries = NativeCore.entries(apk);
        var preservation = preserve(originalEntries, preparedEntries);

        need(
                Arrays.equals(
                        PhoneManifest.add(
                                ManifestLibraries.add(
                                        originalEntries.get("AndroidManifest.xml"),
                                        List.of("QGAPI", "QGMOD", "QGVehicle"))),
                        preparedEntries.get("AndroidManifest.xml")),
                "Only exact production manifest additions permitted");
        need(
                Arrays.equals(
                        Files.readAllBytes(builder.resolve("component:qg-runtime")),
                        preparedEntries.get("lib/arm64-v8a/libqg_runtime.so")),
                "Exact production runtime library required");

        return preservation;
    }

    static int checkBadging(Toolchains tools, Path stage, Path apk) throws Exception {
        String badging =
                Processes.run(
                                tools.buildTool("aapt2"),
                                List.of("dump", "badging", apk.toString()),
                                stage,
                                Duration.ofSeconds(30),
                                false)
                        .stdout();

        need(badging.contains("package: name='" + PACKAGE + "'"), "Exact OEM package required");
        var sdkMatch =
                java.util.regex.Pattern.compile("(?m)^sdkVersion:'([0-9]+)'$").matcher(badging);
        need(sdkMatch.find(), "Numeric minimum SDK required");
        int minSdk = Integer.parseInt(sdkMatch.group(1));
        need(minSdk > 0 && minSdk <= 28, "API28-compatible minimum SDK required");
        Files.writeString(
                stage.resolve("aapt-badging.txt"), badging, StandardOpenOption.CREATE_NEW);

        return minSdk;
    }

    static void checkSignature(Path apk) throws Exception {
        var signature =
                new com.android.apksig.ApkVerifier.Builder(apk.toFile())
                        .setMinCheckedPlatformVersion(28)
                        .setMaxCheckedPlatformVersion(28)
                        .build()
                        .verify();

        need(
                signature.isVerified()
                        && signature.getSignerCertificates().size() == 1
                        && CERT.equals(
                                NativeCore.hash(
                                        signature.getSignerCertificates().get(0).getEncoded())),
                "Actual API28 platform signature differs");
        need(
                Files.size(apk) > 0 && Files.size(apk) <= 128L * 1024 * 1024,
                "Bounded Phone APK required");
    }

    static Map<String, Object> artifactReceipt(Path apk, int minSdk) throws Exception {
        return Map.of(
                "file",
                apk.toString(),
                "sha256",
                FilesSupport.hash(apk),
                "bytes",
                Files.size(apk),
                "package",
                PACKAGE,
                "certificate_sha256",
                CERT,
                "signature_verified_api",
                28,
                "signer_count",
                1,
                "min_sdk",
                minSdk,
                "package_verified",
                true);
    }
}
