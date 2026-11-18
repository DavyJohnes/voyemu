package com.voyemu.prepare;

import static java.nio.charset.StandardCharsets.UTF_8;

import com.android.apksig.ApkVerifier;

import java.io.ByteArrayInputStream;
import java.lang.reflect.InvocationTargetException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.KeyStore;
import java.security.cert.CertificateFactory;
import java.security.spec.InvalidKeySpecException;
import java.util.Map;
import java.util.TreeMap;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * Exercises Artifacts' real signing path with a valid own APK; without explicit inputs it signs
 * with a synthetic key, so it never needs the platform key that fetch downloads.
 */
public final class SigningIntermediateTest {
    public static void main(String[] args) throws Exception {
        if (args.length != 1 && args.length != 4) {
            throw new IllegalArgumentException("ROOT [OWN_APK KEY CERTIFICATE]");
        }

        Path project = Path.of(args[0]).toRealPath();
        Path root =
                Files.createTempDirectory(
                        project.resolve(".local/build/tmp"), "signing-intermediate-");

        try {
            Path apk = args.length == 4 ? Path.of(args[1]).toRealPath() : ownApk(project, root);
            Path key, certificate;

            if (args.length == 4) {
                key = Path.of(args[2]).toRealPath();
                certificate = Path.of(args[3]).toRealPath();
            } else {
                key = root.resolve("synthetic.pk8");
                certificate = root.resolve("synthetic.x509.der");
                syntheticSigning(root, key, certificate);
            }

            Storage storage =
                    Storage.resolve(root.resolve("home").toString(), Map.of(), "mac", root);
            var profile =
                    new ProfileRegistry.Profile(
                            "fixture",
                            "test",
                            Map.of(
                                    "android",
                                    Map.of(
                                            "package",
                                            "system-images;android-30;default;arm64-v8a")),
                            "");
            var entries = new TreeMap<>(NativeCore.entries(apk));
            entries.keySet().removeIf(DexOperations::signatureEntry);
            entries.put(
                    "assets/signing-intermediate-fixture.txt",
                    "own fixture".getBytes(java.nio.charset.StandardCharsets.UTF_8));
            entries.put("lib/arm64-v8a/libprobe-one.so", new byte[23800]);
            entries.put("lib/arm64-v8a/libprobe-two.so", new byte[36440]);
            entries.put("lib/arm64-v8a/libprobe-three.so", new byte[47384]);

            for (int i = 0; i < 128; i++) {
                entries.put("assets/prefix-" + i + ".bin", new byte[i + 1]);
            }

            Path unsignedLayout = root.resolve("unsigned-layout.apk");
            NativeCore.zip(unsignedLayout, entries, true);
            checkNativeLibraries(unsignedLayout, false);
            var method =
                    Artifacts.class.getDeclaredMethod("signed", Map.class, Path.class, int.class);
            method.setAccessible(true);
            var tools = new Toolchains(storage, Map.of(), root, key, certificate);
            var builder =
                    new Artifacts(
                            new Artifacts.PatchedProfile(profile, Map.of()),
                            storage,
                            tools,
                            root,
                            root.resolve("work"),
                            path -> {
                                throw new AssertionError("No Android input read");
                            },
                            message -> {});
            Path success = root.resolve("success");
            Storage.directory(success);
            Path sibling = success.resolve("keep.txt");
            Files.writeString(sibling, "keep");
            Path signed = (Path) method.invoke(builder, entries, success, 28);

            if (!signed.equals(success.resolve("signed.apk"))
                    || !Files.isRegularFile(signed)
                    || Files.exists(success.resolve("unsigned.apk"))) {
                throw new AssertionError(
                        "Verified signing must return signed APK and remove only its unsigned"
                                + " intermediate");
            }

            checkSignedOutput(signed, certificate, entries);
            checkNativeLibraries(signed, true);

            if (!Files.readString(sibling).equals("keep")) {
                throw new AssertionError("Sibling file changed");
            }

            checkFailedSigning(root, storage, profile, certificate, method, entries);
        } finally {
            FilesSupport.removeOwnedTree(root);
        }

        System.out.println(
                "Verified-signing intermediate cleanup and failed-signing preservation passed");
    }

    static Path signingInput(Path project, Map<String, Object> signing, String role)
            throws Exception {
        String pin = Json.str(Json.obj(signing, role), "sha256");
        Path file = project.resolve(".local/toolchains/downloads/native/" + pin + ".blob");

        if (!FilesSupport.hash(file).equals(pin)) {
            throw new AssertionError("Pinned signing input differs: " + role);
        }

        return file;
    }

    /** Writes a fresh RSA key as PKCS#8 and its self-signed certificate made by the JDK keytool. */
    static void syntheticSigning(Path root, Path key, Path certificate) throws Exception {
        Path store = root.resolve("synthetic.p12");
        String executable = System.getProperty("os.name").startsWith("Windows") ? ".exe" : "";
        Path keytool = Path.of(System.getProperty("java.home"), "bin", "keytool" + executable);
        Process process =
                new ProcessBuilder(
                                keytool.toString(),
                                "-genkeypair",
                                "-keyalg",
                                "RSA",
                                "-keysize",
                                "2048",
                                "-validity",
                                "3650",
                                "-dname",
                                "CN=Voyemu signing test",
                                "-alias",
                                "signing",
                                "-storetype",
                                "PKCS12",
                                "-keystore",
                                store.toString(),
                                "-storepass",
                                "synthetic")
                        .redirectErrorStream(true)
                        .start();
        String output = new String(process.getInputStream().readAllBytes(), UTF_8);

        if (process.waitFor() != 0) {
            throw new AssertionError("keytool failed: " + output);
        }

        var keys = KeyStore.getInstance("PKCS12");

        try (var input = Files.newInputStream(store)) {
            keys.load(input, "synthetic".toCharArray());
        }

        Files.write(key, keys.getKey("signing", "synthetic".toCharArray()).getEncoded());
        Files.write(certificate, keys.getCertificate("signing").getEncoded());
    }

    static Path ownApk(Path project, Path root) throws Exception {
        Path source = root.resolve("source"), work = root.resolve("resources");

        Storage.directory(source.resolve("res/values"));
        Storage.directory(work);
        Files.writeString(
                source.resolve("AndroidManifest.xml"),
                "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\""
                        + " package=\"own.test.signing\"><uses-sdk android:minSdkVersion=\"26\""
                        + " android:targetSdkVersion=\"28\"/><application android:hasCode=\"false\""
                        + " android:extractNativeLibs=\"false\"/></manifest>");
        Files.writeString(
                source.resolve("res/values/strings.xml"),
                "<resources><string name=\"fixture\">signing</string></resources>");
        String executable = System.getProperty("os.name").startsWith("Windows") ? ".exe" : "";

        return Artifacts.linkComponentResources(
                project.resolve(".local/toolchains/java-sdk/build-tools/36.1.0/aapt2" + executable),
                project.resolve(".local/toolchains/java-sdk/platforms/android-34/android.jar"),
                source,
                work,
                26,
                28);
    }

    static void checkNativeLibraries(Path signed, boolean verifySignature) throws Exception {
        byte[] bytes = Files.readAllBytes(signed);
        ByteBuffer buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        int found = 0;

        try (ZipFile zip = new ZipFile(signed.toFile())) {
            for (int at = 0; buffer.getInt(at) == 0x04034b50; ) {
                int nameLength = Short.toUnsignedInt(buffer.getShort(at + 26));
                int extraLength = Short.toUnsignedInt(buffer.getShort(at + 28));
                String name =
                        new String(
                                bytes,
                                at + 30,
                                nameLength,
                                java.nio.charset.StandardCharsets.UTF_8);
                ZipEntry entry = zip.getEntry(name);
                int data = at + 30 + nameLength + extraLength;

                if (name.startsWith("lib/") && name.endsWith(".so")) {
                    if (entry.getMethod() != ZipEntry.STORED || data % 4096 != 0) {
                        throw new AssertionError(
                                "Signed native library requires uncompressed page alignment: "
                                        + name
                                        + " offset="
                                        + data);
                    }

                    for (int extra = at + 30 + nameLength; extra + 4 <= data; ) {
                        int tag = Short.toUnsignedInt(buffer.getShort(extra));
                        int length = Short.toUnsignedInt(buffer.getShort(extra + 2));

                        if (tag == 0xd935
                                && (length < 2
                                        || Short.toUnsignedInt(buffer.getShort(extra + 4))
                                                != 4096)) {
                            throw new AssertionError(
                                    "Signed native ZIP alignment metadata differs: " + name);
                        }

                        extra += 4 + length;
                    }

                    found++;
                }

                at = Math.toIntExact(data + entry.getCompressedSize());
            }
        }

        if (found != 3) {
            throw new AssertionError("Expected all three signed native library entries");
        }

        if (!verifySignature) {
            return;
        }

        var schemes =
                new ApkVerifier.Builder(signed.toFile())
                        .setMinCheckedPlatformVersion(23)
                        .setMaxCheckedPlatformVersion(27)
                        .build()
                        .verify();

        if (!schemes.isVerifiedUsingV1Scheme() || !schemes.isVerifiedUsingV2Scheme()) {
            throw new AssertionError("Signed native APK must verify both v1 and v2 signatures");
        }
    }

    static void checkSignedOutput(Path signed, Path certificate, Map<String, byte[]> entries)
            throws Exception {
        var verified =
                new ApkVerifier.Builder(signed.toFile())
                        .setMinCheckedPlatformVersion(30)
                        .setMaxCheckedPlatformVersion(30)
                        .build()
                        .verify();
        var expectedCertificate =
                CertificateFactory.getInstance("X.509")
                        .generateCertificate(
                                new ByteArrayInputStream(Files.readAllBytes(certificate)));

        if (!verified.isVerified()
                || verified.getSignerCertificates().size() != 1
                || !NativeCore.hash(verified.getSignerCertificates().get(0).getEncoded())
                        .equals(NativeCore.hash(expectedCertificate.getEncoded()))) {
            throw new AssertionError("Successful output must retain verified signing identity");
        }

        var payload = new TreeMap<>(NativeCore.entries(signed));
        payload.keySet().removeIf(DexOperations::signatureEntry);

        if (!payload.keySet().equals(entries.keySet())) {
            throw new AssertionError("Signed payload inventory changed");
        }

        for (var entry : entries.entrySet()) {
            if (!java.util.Arrays.equals(entry.getValue(), payload.get(entry.getKey()))) {
                throw new AssertionError("Signed payload changed: " + entry.getKey());
            }
        }
    }

    static void checkFailedSigning(
            Path root,
            Storage storage,
            ProfileRegistry.Profile profile,
            Path certificate,
            java.lang.reflect.Method method,
            Map<String, byte[]> entries)
            throws Exception {
        Path badKey = root.resolve("invalid.pk8");

        Files.write(badKey, new byte[] {0});
        var failingTools = new Toolchains(storage, Map.of(), root, badKey, certificate);
        var failing =
                new Artifacts(
                        new Artifacts.PatchedProfile(profile, Map.of()),
                        storage,
                        failingTools,
                        root,
                        root.resolve("failed-work"),
                        path -> {
                            throw new AssertionError("No Android input read");
                        },
                        message -> {});
        Path failure = root.resolve("failure");
        Storage.directory(failure);

        try {
            method.invoke(failing, entries, failure, 30);
            throw new AssertionError("Invalid signing key accepted");
        } catch (InvocationTargetException error) {
            if (!(error.getCause() instanceof InvalidKeySpecException)) {
                throw error;
            }
        }

        Path unsigned = failure.resolve("unsigned.apk");

        if (!Files.isRegularFile(unsigned) || Files.exists(failure.resolve("signed.apk"))) {
            throw new AssertionError(
                    "Signing failure must retain unsigned diagnostic input without a successful"
                            + " output");
        }

        var retained = NativeCore.entries(unsigned);

        if (!retained.keySet().equals(entries.keySet())) {
            throw new AssertionError("Failed-signing diagnostic inventory changed");
        }

        for (var entry : entries.entrySet()) {
            if (!java.util.Arrays.equals(entry.getValue(), retained.get(entry.getKey()))) {
                throw new AssertionError("Failed-signing diagnostic payload changed");
            }
        }
    }
}
