package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Build the real API28 QGMedia recipe and check retained OEM USB semantics and signed helper. */
public final class UsbPlayerArtifactPipelineTest {
    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static Map<String, ClassDef> classes(byte[] dex) {
        var out = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(28), dex).getClasses()) {
            out.put(c.getType(), c);
        }

        return out;
    }

    static void verifyWheelInitialization(
            Map<String, byte[]> originalEntries, Map<String, byte[]> outputEntries)
            throws Exception {
        var outputMain = classes(outputEntries.get("classes.dex"));
        var application = outputMain.get("Lcom/qinggan/media/MediaApplication;");
        var onCreate =
                java.util.stream.StreamSupport.stream(application.getMethods().spliterator(), false)
                        .filter(m -> m.getName().equals("onCreate"))
                        .findFirst()
                        .orElseThrow();
        var calls = new ArrayList<String>();

        for (var instruction : onCreate.getImplementation().getInstructions()) {
            if (instruction instanceof org.jf.dexlib2.iface.instruction.ReferenceInstruction ref) {
                calls.add(ref.getReference().toString());
            }
        }

        int media =
                calls.indexOf(
                        "Lcom/qinggan/app/mediaCentre/MediaManager;->getInstance()Lcom/qinggan/app/mediaCentre/MediaManager;");
        int keys =
                calls.indexOf(
                        "Lcom/qingang/asgard/media/general/manager/MediaKeyManager;->getInstance(Landroid/content/Context;)Lcom/qingang/asgard/media/general/manager/MediaKeyManager;");
        check(
                media >= 0 && keys > media,
                "MediaManager must initialize before key registration on Application main thread");
        var originalKeys = classes(originalEntries.get("classes2.dex"));
        var outputKeys = classes(outputEntries.get("classes2.dex"));
        check(
                FrameworkDexMaterializer.semantic(
                                originalKeys.get("Lcom/qinggan/app/mediaCentre/MediaManager;"))
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        outputKeys.get(
                                                "Lcom/qinggan/app/mediaCentre/MediaManager;"))),
                "OEM MediaManager changed");

        for (var entry : originalKeys.entrySet()) {
            if (entry.getKey()
                    .startsWith("Lcom/qingang/asgard/media/general/manager/MediaKeyManager")) {
                check(
                        FrameworkDexMaterializer.semantic(entry.getValue())
                                .equals(
                                        FrameworkDexMaterializer.semantic(
                                                outputKeys.get(entry.getKey()))),
                        "OEM key handler changed: " + entry.getKey());
            }
        }
    }

    // Counts the guard helper class across every DEX of the APK.
    static int helperCount(Map<String, byte[]> entries) {
        int helperCount = 0;

        for (var entry : entries.entrySet()) {
            if (entry.getKey().matches("classes[0-9]*\\.dex")) {
                for (String type : classes(entry.getValue()).keySet()) {
                    if (type.equals(Api28UsbPlayerGuard.HELPER)) {
                        helperCount++;
                    }
                }
            }
        }

        return helperCount;
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]).toRealPath(),
                requestFile = Path.of(args[1]).toRealPath(),
                work = Path.of(args[2]).toAbsolutePath();
        var request = Json.object(Json.parse(Files.readString(requestFile)));
        var selected = Json.obj(request, "tools");
        var signing = Json.obj(selected, "signing");
        var platform = Json.obj(selected, "platform");

        for (var p :
                List.of(
                        Map.entry("key", "key_sha256"),
                        Map.entry("certificate", "certificate_sha256"))) {
            check(
                    FilesSupport.hash(Path.of(Json.str(signing, p.getKey())))
                            .equals(Json.str(signing, p.getValue())),
                    "signing input pin");
        }

        Path manifest = root.resolve("src/app/toolchain.json");
        check(
                FilesSupport.hash(manifest).equals(Json.str(selected, "manifest_sha256")),
                "toolchain pin");
        check(
                FilesSupport.hash(Path.of(Json.str(platform, "file")))
                        .equals(Json.str(platform, "sha256")),
                "platform pin");
        var storage =
                new Storage(
                        root.resolve(".local"),
                        work.resolve("config"),
                        work.resolve("cache"),
                        work.resolve("state"),
                        work.resolve("run"),
                        work.resolve("logs"));
        var tools =
                new Toolchains(
                        storage,
                        Json.object(Json.parse(Files.readString(manifest))),
                        Path.of(Json.str(selected, "sdk")),
                        Path.of(Json.str(signing, "key")),
                        Path.of(Json.str(signing, "certificate")),
                        Path.of(Json.str(platform, "file")));
        var profile =
                new ProfileRegistry(root, TestProfiles.declaring(root, Api28UsbPlayerGuard.NAME))
                        .get(TestProfiles.declaring(root, Api28UsbPlayerGuard.NAME));
        var artifacts =
                new Artifacts(
                        profile,
                        storage,
                        tools,
                        root.resolve("profiles").resolve(profile.id()),
                        work.resolve("artifacts"),
                        p -> {
                            throw new AssertionError(
                                    "QGMedia must not extract Android files: " + p);
                        },
                        message -> System.err.println(message));
        Path output =
                args.length == 4
                        ? Path.of(args[3]).toRealPath()
                        : artifacts.resolve("patched:QGMedia");
        Path original = root.resolve(".local/profiles/" + profile.id() + "/originals/QGMedia.apk");
        var originalEntries = NativeCore.entries(original);
        var outputEntries = NativeCore.entries(output);
        var before = classes(originalEntries.get("classes3.dex"));
        var after = classes(outputEntries.get("classes3.dex"));

        for (var entry : before.entrySet()) {
            if (entry.getKey().startsWith("Lcom/qinggan/media/usbmusic/")
                    && !Api28UsbPlayerGuard.TARGETS.contains(entry.getKey())) {
                check(
                        FrameworkDexMaterializer.semantic(
                                        Api28UsbPlayerGuard.canonicalClass(entry.getValue()))
                                .equals(
                                        FrameworkDexMaterializer.semantic(
                                                Api28UsbPlayerGuard.canonicalClass(
                                                        after.get(entry.getKey())))),
                        "OEM USB class altered: " + entry.getKey());
            }
        }

        check(
                after.get(Api28UsbPlayerGuard.PLAYER + ";").getFields().iterator().hasNext(),
                "Player absent");

        for (var field : after.get(Api28UsbPlayerGuard.PLAYER + ";").getFields()) {
            if (field.getName().equals("mMediaPlayer")) {
                check(
                        field.getType().equals(Api28UsbPlayerGuard.HELPER),
                        "real producer lost decoder patch");
            }
        }

        for (var type : after.values()) {
            if (type.getType().startsWith("Lcom/qinggan/media/usbmusic/")) {
                check(
                        !FrameworkDexMaterializer.semantic(type)
                                .contains(Api28UsbPlayerGuard.ORIGINAL),
                        "USB path still references unavailable native Ijk decoder: "
                                + type.getType());
            }
        }

        int helperCount = helperCount(outputEntries);
        check(helperCount == 1, "actual helper missing/duplicated");
        verifyWheelInitialization(originalEntries, outputEntries);
        var signature =
                new com.android.apksig.ApkVerifier.Builder(output.toFile())
                        .setMinCheckedPlatformVersion(28)
                        .build()
                        .verify();
        check(
                signature.isVerified() && signature.getSignerCertificates().size() == 1,
                "actual APK signature");
        byte[] expectedCertificate;

        try (var stream = Files.newInputStream(Path.of(Json.str(signing, "certificate")))) {
            expectedCertificate =
                    java.security.cert.CertificateFactory.getInstance("X.509")
                            .generateCertificate(stream)
                            .getEncoded();
        }

        check(
                java.util.Arrays.equals(
                        signature.getSignerCertificates().get(0).getEncoded(), expectedCertificate),
                "actual signer differs");
        var receipt =
                Map.of(
                        "profile_sha256",
                        FilesSupport.hash(
                                root.resolve("profiles/" + profile.id() + "/profile.json")),
                        "signed_apk",
                        output.toString(),
                        "signed_apk_sha256",
                        FilesSupport.hash(output),
                        "original_usb_queue_preserved",
                        true,
                        "platform_decoder",
                        true,
                        "helper_count",
                        helperCount,
                        "platform_signature_verified",
                        true,
                        "guest_started",
                        false,
                        "existing_output_verification",
                        args.length == 4);
        Files.writeString(work.resolve("receipt.json"), Json.write(receipt) + "\n");
        System.out.println(Json.write(receipt));
    }
}
