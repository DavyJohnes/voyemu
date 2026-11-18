package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

/**
 * Bounded ordinary Artifacts graph proof; owns only this job's source fixture and writable outputs.
 */
public final class PhonePackageTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    static void need(boolean value, String reason) {
        if (!value) {
            throw new AssertionError(reason);
        }
    }

    static void link(Path source, Path target) throws Exception {
        Files.createDirectories(target.getParent());
        Files.createLink(target, source);
    }

    static void copyOriginal(Path source, Path target) throws Exception {
        Files.createDirectories(target.getParent());
        Files.copy(source, target);
    }

    static Set<String> classes(byte[] dex) throws Exception {
        return NativeCore.classes(
                new org.jf.dexlib2.dexbacked.DexBackedDexFile(Opcodes.forApi(30), dex));
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), job = Path.of(args[1]), apk = Path.of(args[2]);
        String id = PROFILE;
        Path sourceRoot = job.resolve("ordinary-profile"),
                source = sourceRoot.resolve("profiles/" + id);

        need(!Files.exists(sourceRoot), "Fresh isolated fixture required");
        stageSources(root, job, Path.of(args[3]), sourceRoot, source);
        var originalProfile =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve("profiles/" + id + "/profile.json"))));
        var expectedPhoneInstall = expectedPhoneInstall();
        checkSelectedInstall(Path.of(args[3]), expectedPhoneInstall);

        var qgapi = Json.object(Json.obj(originalProfile, "original").get("QGAPI"));
        writeProfile(source, id, originalProfile, qgapi, expectedPhoneInstall);
        var registry = new ProfileRegistry(sourceRoot, id);
        var profile = registry.get(id);
        Path owned = job.resolve("ordinary-storage");
        var storage =
                new Storage(
                        owned,
                        owned.resolve("config"),
                        owned.resolve("cache"),
                        owned.resolve("state"),
                        owned.resolve("run"),
                        owned.resolve("logs"));
        copyOriginal(apk, storage.originals(id).resolve("BluetoothPhone.apk"));
        Path framework =
                root.resolve(".local/profiles/" + id + "/originals/" + Json.str(qgapi, "file"));
        copyOriginal(framework, storage.originals(id).resolve("QGAPI.jar"));
        var signing =
                Json.obj(
                        Json.object(
                                Json.parse(
                                        Files.readString(
                                                root.resolve(
                                                        ".local/profiles/"
                                                                + id
                                                                + "/native-inputs.json")))),
                        "signing");
        Path key = Path.of(Json.str(Json.obj(signing, "key"), "file")),
                cert = Path.of(Json.str(Json.obj(signing, "cert"), "file"));
        NativeCore.pinned(key, Json.str(Json.obj(signing, "key"), "sha256"));
        NativeCore.pinned(cert, Json.str(Json.obj(signing, "cert"), "sha256"));
        NativeCore.pinned(apk, PhoneProviderGuard.pins(PROFILE).get(0));
        NativeCore.pinned(framework, Json.str(qgapi, "sha256"));
        Path sdk = root.resolve(".local/toolchains/java-sdk"),
                platform = sdk.resolve("platforms/android-34/android.jar");
        var manifest =
                Json.object(Json.parse(Files.readString(root.resolve("src/app/toolchain.json"))));
        var tools = new Toolchains(storage, manifest, sdk, key, cert, platform);
        var builder =
                new Artifacts(
                        profile,
                        storage,
                        tools,
                        registry.sourceDirectory(profile),
                        job.resolve("ordinary-work"),
                        p -> {
                            throw new AssertionError("No Android image read allowed: " + p);
                        },
                        System.err::println);
        Path peer = builder.resolve("component:bluetooth"),
                output = builder.resolve("patched:BluetoothPhone");
        need(
                output.equals(builder.resolve("patched:BluetoothPhone")),
                "Repeated artifact resolution differs");
        var before = NativeCore.entries(apk);
        var after = NativeCore.entries(output);
        Set<String> added = new TreeSet<>(after.keySet());
        added.removeAll(before.keySet());
        int[] counts = checkOriginalEntries(apk, before, after);
        int originalDex = counts[0], resources = counts[1];
        var expected =
                ManifestLibraries.add(
                        PhoneProviderGuard.manifest(
                                id, 30, "arm64-v8a", apk, before.get("AndroidManifest.xml")),
                        List.of("QGAPI", "QGMOD", "QGVehicle"));
        need(
                Arrays.equals(expected, after.get("AndroidManifest.xml")),
                "Exact manifest-only additions required");
        PhoneManifestPreserveTest.preserve(
                before.get("AndroidManifest.xml"),
                PhoneProviderGuard.manifest(
                        id, 30, "arm64-v8a", apk, before.get("AndroidManifest.xml")));
        Set<String> providers = checkAddedProviders(added, after);
        String certificate = verifySignature(output, cert);
        registry.verifySource(profile);
        NativeCore.pinned(apk, PhoneProviderGuard.pins(PROFILE).get(0));
        NativeCore.pinned(framework, Json.str(qgapi, "sha256"));
        var receipt = new LinkedHashMap<String, Object>();
        receipt.put("schema", 1);
        receipt.put("ordinary_artifacts_graph", true);
        receipt.put("phone_install", expectedPhoneInstall);
        receipt.put("retained_userdata_delivery", "system-image");
        receipt.put("api", 30);
        receipt.put("source_integrity_sha256", PhoneProviderGuard.pins(PROFILE).get(0));
        receipt.put("semantic_consumer_checked", true);
        receipt.put(
                "artifact",
                Map.of(
                        "file",
                        output.toString(),
                        "sha256",
                        FilesSupport.hash(output),
                        "bytes",
                        Files.size(output)));
        receipt.put("peer", Map.of("file", peer.toString(), "sha256", FilesSupport.hash(peer)));
        receipt.put("original_dex_entries_checked", originalDex);
        receipt.put("window_policy", PhoneCallWindowGuard.policy(PROFILE));
        receipt.put("original_non_code_entries_byte_identical", resources);
        receipt.put("added_provider_classes", providers);
        receipt.put("platform_certificate_sha256", certificate);
        receipt.put("signature_verified_api", 30);
        receipt.put("canonical_mutations", false);
        receipt.put("runtime_verified", false);
        receipt.put("host_capability_enabled", false);
        Files.writeString(
                job.resolve("ordinary-producer-receipt.json"), Json.write(receipt) + "\n");
        System.out.println(Json.write(receipt));
    }

    static void stageSources(Path root, Path job, Path selected, Path sourceRoot, Path source)
            throws Exception {
        Files.createDirectories(source.resolve("patches"));
        Files.copy(
                job.resolve(PhoneProviderGuard.NAME + ".json"),
                source.resolve("patches/" + PhoneProviderGuard.NAME + ".json"));
        Path peerRecipe = selected.resolve("components/bluetooth/component.json");
        Files.createDirectories(source.resolve("components/bluetooth"));
        Files.copy(peerRecipe, source.resolve("components/bluetooth/component.json"));
        var component = Json.object(Json.parse(Files.readString(peerRecipe)));

        for (var e : Json.obj(component, "sourceFiles").values()) {
            String resource = ((String) e).substring("voyemu/".length());
            link(root.resolve(resource), sourceRoot.resolve(resource));
        }

        for (Object path : PhoneProviderGuard.sourceInventory(PROFILE)) {
            link(root.resolve((String) path), sourceRoot.resolve((String) path));
        }
    }

    static Map<String, String> expectedPhoneInstall() {
        return Map.of(
                "artifact",
                "patched:BluetoothPhone",
                "type",
                "apk",
                "target",
                "/system/priv-app/BluetoothPhone-release-signed/BluetoothPhone-release-signed.apk",
                "package",
                "com.qinggan.bluetoothphone",
                "mode",
                "0644");
    }

    static void checkSelectedInstall(Path selected, Map<String, String> expectedPhoneInstall)
            throws Exception {
        var selectedProfile =
                Json.object(Json.parse(Files.readString(selected.resolve("profile.json"))));
        var actualPhoneInstall =
                Json.list(selectedProfile.get("install")).stream()
                        .map(Json::object)
                        .filter(n -> "patched:BluetoothPhone".equals(n.get("artifact")))
                        .toList();

        need(
                actualPhoneInstall.equals(List.of(expectedPhoneInstall)),
                "Phone must be installed into its OEM system path for retained-userdata upgrades");
    }

    static void writeProfile(
            Path source,
            String id,
            Map<String, Object> originalProfile,
            Map<String, Object> qgapi,
            Map<String, String> expectedPhoneInstall)
            throws Exception {
        var originals =
                Map.of(
                        "BluetoothPhone",
                        Map.of(
                                "file",
                                "BluetoothPhone.apk",
                                "sha256",
                                PhoneProviderGuard.pins(PROFILE).get(0)),
                        "QGAPI",
                        Map.of("file", "QGAPI.jar", "sha256", qgapi.get("sha256")));
        var patch =
                Map.of(
                        "type",
                        "apk",
                        "input",
                        "original:BluetoothPhone",
                        "steps",
                        List.of(PhoneProviderGuard.NAME + ".json"),
                        "sign",
                        "platform");
        Map<String, Object> data = new LinkedHashMap<>();

        data.put("schema", 1);
        data.put("id", id);
        data.put("status", "ready");
        data.put("android", originalProfile.get("android"));
        data.put("original", originals);
        data.put("patches", Map.of("BluetoothPhone", patch));
        data.put("components", Map.of("bluetooth", "components/bluetooth"));
        data.put(
                "install",
                List.of(
                        expectedPhoneInstall,
                        Map.of(
                                "artifact",
                                "component:bluetooth",
                                "type",
                                "file",
                                "target",
                                "/system/framework/Ivt8BluetoothAdapter.jar",
                                "mode",
                                "0644")));
        Files.writeString(source.resolve("profile.json"), Json.write(data) + "\n");
    }

    /** Returns the checked original dex entry count and the byte-identical non-code entry count. */
    static int[] checkOriginalEntries(
            Path apk, Map<String, byte[]> before, Map<String, byte[]> after) throws Exception {
        int originalDex = 0, resources = 0;

        for (var e : before.entrySet()) {
            if (e.getKey().equals("AndroidManifest.xml")
                    || DexOperations.signatureEntry(e.getKey())) {
                continue;
            }

            if (e.getKey().equals("classes.dex")) {
                PhoneDisconnectGuard.verify(PROFILE, apk, e.getValue(), after.get(e.getKey()));
            } else {
                need(
                        Arrays.equals(e.getValue(), after.get(e.getKey())),
                        "Original entry changed: " + e.getKey());
            }

            if (e.getKey().matches("classes[0-9]*\\.dex")) {
                originalDex++;
            } else {
                resources++;
            }
        }

        return new int[] {originalDex, resources};
    }

    static Set<String> checkAddedProviders(Set<String> added, Map<String, byte[]> after)
            throws Exception {
        Set<String> providers = new TreeSet<>();

        for (String name : added) {
            need(
                    name.matches("classes[0-9]*\\.dex") || DexOperations.signatureEntry(name),
                    "Unexpected APK addition " + name);

            if (name.endsWith(".dex")) {
                providers.addAll(classes(after.get(name)));
            }
        }

        need(
                providers.contains("Lcom/voyemu/input/PhoneControlProvider;")
                        && providers.contains("Lcom/voyemu/input/PhonePeerProvider;")
                        && providers.stream()
                                .allMatch(n ->
                                        n.matches(
                                                "Lcom/voyemu/input/Phone(?:Control|Peer)Provider(?:\\$[^;]+)?;")),
                "Only owned provider classes added");

        return providers;
    }

    static String verifySignature(Path output, Path cert) throws Exception {
        var signature =
                new com.android.apksig.ApkVerifier.Builder(output.toFile())
                        .setMinCheckedPlatformVersion(30)
                        .setMaxCheckedPlatformVersion(30)
                        .build()
                        .verify();

        need(
                signature.isVerified() && signature.getSignerCertificates().size() == 1,
                "Actual API30 signature failed");
        String certificate =
                NativeCore.hash(
                        java.security.cert.CertificateFactory.getInstance("X.509")
                                .generateCertificate(
                                        new java.io.ByteArrayInputStream(Files.readAllBytes(cert)))
                                .getEncoded());
        need(
                certificate.equals(
                        NativeCore.hash(signature.getSignerCertificates().get(0).getEncoded())),
                "Actual platform signer differs");

        return certificate;
    }
}
