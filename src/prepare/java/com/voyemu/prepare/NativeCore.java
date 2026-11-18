package com.voyemu.prepare;

import com.android.apksig.ApkSigner;
import com.android.apksig.ApkVerifier;
import com.android.tools.r8.D8;
import com.android.tools.r8.D8Command;
import com.android.tools.r8.OutputMode;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Format;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.analysis.AnalyzedInstruction;
import org.jf.dexlib2.analysis.ClassPath;
import org.jf.dexlib2.analysis.ClassPathResolver;
import org.jf.dexlib2.analysis.MethodAnalyzer;
import org.jf.dexlib2.dexbacked.OatFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;

import java.io.ByteArrayInputStream;
import java.io.FilterOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.Reader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.security.KeyFactory;
import java.security.MessageDigest;
import java.security.PrivateKey;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.security.spec.PKCS8EncodedKeySpec;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.regex.Pattern;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

/** A source-built host-only compatibility probe. No subprocess APIs are used. */
public final class NativeCore {
    static final int API = 28;
    static final String MARKER = "Lcom/voyemu/prepare/fixture/BuildMarker;";
    static final Pattern BOOT = Pattern.compile("boot(?:-[A-Za-z0-9_.-]+)?\\.(?:oat|vdex)");

    static void check(boolean condition, String code) {
        if (!condition) {
            throw new IllegalArgumentException(code);
        }
    }

    static String hash(byte[] bytes) throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
    }

    static byte[] pinned(Path path, String expected) throws Exception {
        check(expected != null && expected.matches("[a-f0-9]{64}"), "PIN_INVALID");
        check(
                Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS) && !Files.isSymbolicLink(path),
                "INPUT_INVALID");
        check(Files.size(path) <= 512L * 1024 * 1024, "INPUT_TOO_LARGE");
        byte[] bytes = Files.readAllBytes(path);
        check(hash(bytes).equals(expected), "INPUT_SHA_MISMATCH");

        return bytes;
    }

    static Set<String> classes(DexFile dex) {
        Set<String> result = new TreeSet<>();

        for (ClassDef type : dex.getClasses()) {
            check(result.add(type.getType()), "DUPLICATE_CLASS");
        }

        check(!result.isEmpty(), "CLASS_INVENTORY_EMPTY");

        return result;
    }

    static void safeEntry(String name) {
        check(
                !name.isEmpty()
                        && !name.startsWith("/")
                        && !name.contains("\\")
                        && !name.contains(":")
                        && !name.contains("\0"),
                "ZIP_ENTRY_INVALID");

        for (String part : name.split("/", -1)) {
            check(!part.equals("..") && !part.equals("."), "ZIP_ENTRY_INVALID");
        }
    }

    static Map<String, byte[]> entries(Path file) throws Exception {
        Map<String, byte[]> result = new TreeMap<>();
        long total = 0;

        try (ZipFile zip = new ZipFile(file.toFile())) {
            for (ZipEntry entry : Collections.list(zip.entries())) {
                safeEntry(entry.getName());
                check(!result.containsKey(entry.getName()), "ZIP_DUPLICATE_ENTRY");

                if (entry.isDirectory()) {
                    continue;
                }

                check(
                        entry.getSize() >= 0 && entry.getSize() <= 256L * 1024 * 1024,
                        "ZIP_ENTRY_EXTENT");
                byte[] bytes;

                try (InputStream source = zip.getInputStream(entry)) {
                    bytes = source.readAllBytes();
                }

                check(bytes.length == entry.getSize(), "ZIP_ENTRY_EXTENT");
                CRC32 crc = new CRC32();
                crc.update(bytes);
                check(crc.getValue() == entry.getCrc(), "ZIP_ENTRY_CRC");
                total += bytes.length;
                check(total <= 512L * 1024 * 1024, "ZIP_TOTAL_EXTENT");
                result.put(entry.getName(), bytes);
            }
        }

        return result;
    }

    private static final class Counter extends FilterOutputStream {
        long count;

        Counter(OutputStream output) {
            super(output);
        }

        @Override
        public void write(int value) throws IOException {
            out.write(value);
            count++;
        }

        @Override
        public void write(byte[] bytes, int offset, int length) throws IOException {
            out.write(bytes, offset, length);
            count += length;
        }
    }

    static void zip(Path destination, Map<String, byte[]> data, boolean align) throws Exception {
        try (Counter count = new Counter(Files.newOutputStream(destination));
                ZipOutputStream output = new ZipOutputStream(count, StandardCharsets.UTF_8)) {
            for (Map.Entry<String, byte[]> item : data.entrySet()) {
                safeEntry(item.getKey());
                ZipEntry entry = new ZipEntry(item.getKey());
                // The 1980 DOS boundary causes the JDK to append a nine-byte
                // timestamp extra field, changing the aligned data offset.
                entry.setTimeLocal(java.time.LocalDateTime.of(2000, 1, 1, 0, 0));
                entry.setMethod(ZipEntry.STORED);
                entry.setSize(item.getValue().length);
                CRC32 crc = new CRC32();
                crc.update(item.getValue());
                entry.setCrc(crc.getValue());

                if (align) {
                    int alignment = item.getKey().endsWith(".so") ? 4096 : 4;
                    long plain =
                            count.count
                                    + 30
                                    + item.getKey().getBytes(StandardCharsets.UTF_8).length;

                    if (plain % alignment != 0) {
                        int padding = (int) ((alignment - ((plain + 6) % alignment)) % alignment);
                        int size = 2 + padding;
                        byte[] extra = new byte[4 + size];
                        extra[0] = 0x35;
                        extra[1] = (byte) 0xd9;
                        extra[2] = (byte) size;
                        extra[3] = (byte) (size >>> 8);
                        extra[4] = (byte) alignment;
                        extra[5] = (byte) (alignment >>> 8);
                        entry.setExtra(extra);
                    }
                }

                output.putNextEntry(entry);
                output.write(item.getValue());
                output.closeEntry();
            }
        }
    }

    /**
     * Restores the DEX code of a boot class path library from its OAT entry: type analysis against
     * the boot class path replaces every quickened instruction with its original form.
     */
    static Map<String, Object> framework(Path classpath, Path originalJar, String name, Path output)
            throws Exception {
        Path oat = classpath.resolve("boot-" + name + ".oat");
        var container = DexFileFactory.loadDexContainer(oat.toFile(), Opcodes.forApi(API));

        check(container instanceof OatFile, "FRAMEWORK_NOT_OAT");
        int oatVersion = ((OatFile) container).getOatVersion();
        Set<String> expected = new TreeSet<>();
        List<ClassDef> restoredClasses = new ArrayList<>();
        List<String> names = container.getDexEntryNames();
        check(!names.isEmpty(), "FRAMEWORK_EMPTY");

        for (String entryName : names) {
            var entry = container.getEntry(entryName);
            ClassPathResolver resolver =
                    new ClassPathResolver(
                            List.of(classpath.toString()), List.of("boot.oat"), List.of(), entry);
            ClassPath path = new ClassPath(resolver.getResolvedClassProviders(), false, oatVersion);

            for (ClassDef type : entry.getDexFile().getClasses()) {
                check(expected.add(type.getType()), "FRAMEWORK_DUPLICATE_CLASS");
                restoredClasses.add(deodex(type, path));
            }
        }

        byte[] dex =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(API), restoredClasses));
        check(
                classes(new org.jf.dexlib2.dexbacked.DexBackedDexFile(Opcodes.forApi(API), dex))
                        .equals(expected),
                "ASSEMBLED_CLASS_LOSS");
        Files.write(output.resolve(name + ".dex"), dex);
        Map<String, byte[]> jar = entries(originalJar);
        jar.put("classes.dex", dex);
        Path restored = output.resolve(name + ".jar");
        zip(restored, jar, false);

        return Map.of(
                "name",
                name,
                "classes",
                expected.size(),
                "oat_version",
                oatVersion,
                "dex_sha256",
                hash(dex),
                "jar_sha256",
                hash(Files.readAllBytes(restored)));
    }

    /** The class with every method analyzed and its quickened instructions restored. */
    static ClassDef deodex(ClassDef type, ClassPath path) {
        List<Method> methods = new ArrayList<>();

        for (Method method : type.getMethods()) {
            var code = method.getImplementation();

            if (code == null) {
                methods.add(method);
                continue;
            }

            MethodAnalyzer analyzer = new MethodAnalyzer(path, method, null, false);
            check(analyzer.getAnalysisException() == null, "DEODEX_UNRESOLVED");
            List<Instruction> instructions = new ArrayList<>();

            for (AnalyzedInstruction analyzed : analyzer.getAnalyzedInstructions()) {
                Instruction instruction = analyzed.getInstruction();
                check(
                        !instruction.getOpcode().odexOnly()
                                && instruction.getOpcode().format
                                        != Format.UnresolvedOdexInstruction
                                && instruction.getCodeUnits()
                                        == analyzed.getOriginalInstruction().getCodeUnits(),
                        "DEODEX_UNRESOLVED");
                instructions.add(instruction);
            }

            methods.add(
                    new ImmutableMethod(
                            method.getDefiningClass(),
                            method.getName(),
                            method.getParameters(),
                            method.getReturnType(),
                            method.getAccessFlags(),
                            method.getAnnotations(),
                            method.getHiddenApiRestrictions(),
                            new ImmutableMethodImplementation(
                                    code.getRegisterCount(),
                                    instructions,
                                    code.getTryBlocks(),
                                    code.getDebugItems())));
        }

        return new ImmutableClassDef(
                type.getType(),
                type.getAccessFlags(),
                type.getSuperclass(),
                type.getInterfaces(),
                type.getSourceFile(),
                type.getAnnotations(),
                type.getFields(),
                methods);
    }

    static Path fixtureDex(Path output) throws Exception {
        Path classFile = output.resolve("BuildMarker.class");

        try (InputStream source =
                NativeCore.class.getResourceAsStream(
                        "/com/voyemu/prepare/fixture/BuildMarker.class")) {
            check(source != null, "OWN_CLASS_RESOURCE_MISSING");
            Files.write(classFile, source.readAllBytes());
        }

        Path dexDir = Files.createDirectory(output.resolve("d8"));
        D8.run(
                D8Command.builder()
                        .addProgramFiles(classFile)
                        .setMinApiLevel(API)
                        .setOutput(dexDir, OutputMode.DexIndexed)
                        .build());
        Path dex = dexDir.resolve("classes.dex");
        var container = DexFileFactory.loadDexContainer(dex.toFile(), Opcodes.forApi(API));
        check(
                classes(container.getEntry(container.getDexEntryNames().get(0)).getDexFile())
                        .equals(Set.of(MARKER)),
                "D8_CLASS_INVALID");

        return dex;
    }

    static Map<String, Object> patchAndSign(
            Path apk, Path markerDex, Path output, byte[] keyBytes, byte[] certBytes)
            throws Exception {
        Map<String, byte[]> original = entries(apk);

        check(
                original.containsKey("AndroidManifest.xml") && original.containsKey("classes.dex"),
                "APK_REQUIRED_ENTRY");
        var entry =
                DexFileFactory.loadDexEntry(apk.toFile(), "classes.dex", true, Opcodes.forApi(API));
        Set<String> expected = classes(entry.getDexFile());
        byte[] rebuilt =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(
                                Opcodes.forApi(API), List.copyOf(entry.getDexFile().getClasses())));
        check(
                classes(new org.jf.dexlib2.dexbacked.DexBackedDexFile(Opcodes.forApi(API), rebuilt))
                        .equals(expected),
                "ASSEMBLED_CLASS_LOSS");
        Map<String, byte[]> patched = new TreeMap<>(original);
        patched.keySet()
                .removeIf(n ->
                        n.toUpperCase(java.util.Locale.ROOT)
                                .matches("META-INF/(?:MANIFEST\\.MF|.*\\.(?:SF|RSA|DSA|EC))"));
        patched.put("classes.dex", rebuilt);
        int slot = 2;

        while (patched.containsKey("classes" + slot + ".dex")) {
            slot++;
        }

        patched.put("classes" + slot + ".dex", Files.readAllBytes(markerDex));
        Path unsigned = output.resolve("patched-unsigned.apk");
        zip(unsigned, patched, true);
        PrivateKey key =
                KeyFactory.getInstance("RSA").generatePrivate(new PKCS8EncodedKeySpec(keyBytes));
        X509Certificate cert =
                (X509Certificate)
                        CertificateFactory.getInstance("X.509")
                                .generateCertificate(new ByteArrayInputStream(certBytes));
        var config =
                new ApkSigner.SignerConfig.Builder("voyemu-native-probe", key, List.of(cert))
                        .build();
        Path signed = output.resolve("patched-signed.apk");
        new ApkSigner.Builder(List.of(config))
                .setInputApk(unsigned.toFile())
                .setOutputApk(signed.toFile())
                .setMinSdkVersion(API)
                .setV1SigningEnabled(true)
                .setV2SigningEnabled(true)
                .setV3SigningEnabled(true)
                .setV4SigningEnabled(false)
                .build()
                .sign();
        var verified =
                new ApkVerifier.Builder(signed.toFile())
                        .setMinCheckedPlatformVersion(API)
                        .setMaxCheckedPlatformVersion(API)
                        .build()
                        .verify();
        // Verifier selects the scheme used by each Android version. A v3
        // result on API 28 does not assert that v1/v2 were exercised.
        var verifiedV2 =
                new ApkVerifier.Builder(signed.toFile())
                        .setMinCheckedPlatformVersion(24)
                        .setMaxCheckedPlatformVersion(27)
                        .build()
                        .verify();
        var verifiedV1 =
                new ApkVerifier.Builder(signed.toFile())
                        .setMinCheckedPlatformVersion(23)
                        .setMaxCheckedPlatformVersion(23)
                        .build()
                        .verify();
        check(
                verified.isVerified()
                        && verified.isVerifiedUsingV3Scheme()
                        && verifiedV2.isVerified()
                        && verifiedV2.isVerifiedUsingV2Scheme()
                        && verifiedV1.isVerified()
                        && verifiedV1.isVerifiedUsingV1Scheme(),
                "APK_SIGNATURE_INVALID");
        check(
                verified.getSignerCertificates().size() == 1
                        && hash(verified.getSignerCertificates().get(0).getEncoded())
                                .equals(hash(cert.getEncoded())),
                "APK_CERTIFICATE_MISMATCH");
        Map<String, byte[]> signedData = entries(signed);
        int preserved = 0;

        for (var item : original.entrySet()) {
            if (item.getKey().equals("classes.dex")
                    || item.getKey().toUpperCase(java.util.Locale.ROOT).startsWith("META-INF/")) {
                continue;
            }

            check(
                    signedData.containsKey(item.getKey())
                            && java.util.Arrays.equals(
                                    item.getValue(), signedData.get(item.getKey())),
                    "APK_RESOURCE_CHANGED");
            preserved++;
        }

        var finalContainer = DexFileFactory.loadDexContainer(signed.toFile(), Opcodes.forApi(API));
        Set<String> finalClasses = new TreeSet<>();

        for (String dexName : finalContainer.getDexEntryNames()) {
            for (String descriptor : classes(finalContainer.getEntry(dexName).getDexFile())) {
                check(finalClasses.add(descriptor), "APK_DUPLICATE_CLASS");
            }
        }

        check(finalClasses.contains(MARKER), "APK_MARKER_MISSING");

        return Map.of(
                "rebuilt_classes_dex_classes",
                expected.size(),
                "total_classes",
                finalClasses.size(),
                "marker_dex",
                "classes" + slot + ".dex",
                "preserved_entries",
                preserved,
                "v1",
                verifiedV1.isVerifiedUsingV1Scheme(),
                "v2",
                verifiedV2.isVerifiedUsingV2Scheme(),
                "v3",
                verified.isVerifiedUsingV3Scheme(),
                "certificate_sha256",
                hash(cert.getEncoded()),
                "apk_sha256",
                hash(Files.readAllBytes(signed)),
                "apk_bytes",
                Files.size(signed));
    }

    static String json(Object value) {
        if (value == null) {
            return "null";
        }

        if (value instanceof Number || value instanceof Boolean) {
            return value.toString();
        }

        if (value instanceof Map<?, ?> map) {
            List<String> entries = new ArrayList<>();

            for (Object key : new TreeSet<>(map.keySet().stream().map(Object::toString).toList())) {
                entries.add(json(key) + ":" + json(map.get(key)));
            }

            return "{" + String.join(",", entries) + "}";
        }

        if (value instanceof Iterable<?> list) {
            List<String> entries = new ArrayList<>();

            for (Object item : list) {
                entries.add(json(item));
            }

            return "[" + String.join(",", entries) + "]";
        }

        String text = value.toString();
        StringBuilder escaped = new StringBuilder("\"");

        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);

            if (c == '"' || c == '\\') {
                escaped.append('\\').append(c);
            } else if (c < 32) {
                escaped.append(String.format("\\u%04x", (int) c));
            } else {
                escaped.append(c);
            }
        }

        return escaped.append('"').toString();
    }

    static void run(Path manifest, Path output, boolean reference) throws Exception {
        long started = System.nanoTime();
        String imageCode = System.getProperty("org.graalvm.nativeimage.imagecode", "jvm");

        check(reference || imageCode.equals("runtime"), "NATIVE_RUNTIME_REQUIRED");
        check(!Files.exists(output, LinkOption.NOFOLLOW_LINKS), "OUTPUT_EXISTS");
        Properties config = new Properties();
        check(Files.size(manifest) <= 512 * 1024, "MANIFEST_TOO_LARGE");

        try (Reader reader = Files.newBufferedReader(manifest)) {
            config.load(reader);
        }

        check("1".equals(config.getProperty("schema")), "MANIFEST_SCHEMA");
        Path framework = Path.of(config.getProperty("framework.root"));
        check(Files.isDirectory(framework) && !Files.isSymbolicLink(framework), "FRAMEWORK_ROOT");
        List<String> selected =
                config.stringPropertyNames().stream()
                        .filter(n -> n.startsWith("framework.pin."))
                        .map(n -> n.substring("framework.pin.".length()))
                        .sorted()
                        .toList();
        check(!selected.isEmpty(), "FRAMEWORK_PINS_MISSING");

        for (String relative : selected) {
            safeEntry(relative);
            pinned(framework.resolve(relative), config.getProperty("framework.pin." + relative));
        }

        byte[] apkBytes =
                pinned(Path.of(config.getProperty("apk.file")), config.getProperty("apk.sha256"));
        byte[] keyBytes =
                pinned(
                        Path.of(config.getProperty("signing.key.file")),
                        config.getProperty("signing.key.sha256"));
        byte[] certBytes =
                pinned(
                        Path.of(config.getProperty("signing.cert.file")),
                        config.getProperty("signing.cert.sha256"));
        Files.createDirectories(output.getParent());
        Files.createDirectory(output);
        Path classpath = Files.createDirectory(output.resolve("classpath"));
        int decoded = 0;

        for (String relative : selected) {
            String name = Path.of(relative).getFileName().toString();

            if (!BOOT.matcher(name).matches()) {
                continue;
            }

            byte[] bytes =
                    pinned(
                            framework.resolve(relative),
                            config.getProperty("framework.pin." + relative));

            if (relative.startsWith("arm64/") && name.endsWith(".oat")) {
                Files.write(classpath.resolve(name), bytes);
            }

            if (!relative.contains("/") && name.endsWith(".vdex")) {
                Files.write(classpath.resolve(name), BootVdex.decode(bytes));
                decoded++;
            }
        }

        Path localApk = output.resolve("original.apk");
        Files.write(localApk, apkBytes);
        List<Map<String, Object>> restored = new ArrayList<>();

        for (String name : List.of("QGAPI", "QGMOD", "QGVehicle")) {
            Path jar = output.resolve(name + "-original.jar");
            Files.write(
                    jar,
                    pinned(
                            framework.resolve(name + ".jar"),
                            config.getProperty("framework.pin." + name + ".jar")));
            System.err.println("Restoring " + name);
            restored.add(framework(classpath, jar, name, output));
        }

        System.err.println("Compiling own bytecode with D8");
        Path marker = fixtureDex(output);
        System.err.println("Rebuilding and signing OEM APK");
        var apk = patchAndSign(localApk, marker, output, keyBytes, certBytes);
        Map<String, Object> report =
                Map.of(
                        "schema",
                        1,
                        "image_code",
                        imageCode,
                        "java_vm",
                        System.getProperty("java.vm.name"),
                        "api",
                        API,
                        "decoded_vdex_files",
                        decoded,
                        "frameworks",
                        restored,
                        "apk",
                        apk,
                        "elapsed_seconds",
                        (System.nanoTime() - started) / 1_000_000_000.0);
        Files.writeString(output.resolve("result.json"), json(report) + "\n");
        System.out.println(json(report));
    }

    public static void main(String[] args) {
        try {
            if (args.length == 1 && args[0].equals("self-test")) {
                NativeCoreTest.run();

                return;
            }

            check(
                    (args.length == 3 || args.length == 4) && args[0].equals("run"),
                    "USAGE_run_INPUTS_OUTPUT");
            boolean reference = args.length == 4 && args[3].equals("--reference-jvm");
            check(args.length == 3 || reference, "UNKNOWN_OPTION");
            run(Path.of(args[1]), Path.of(args[2]), reference);
        } catch (Throwable error) {
            // checkstyle-allow IllegalCatch: process boundary reports every failure
            System.err.println(error.getClass().getSimpleName() + ": " + error.getMessage());
            error.printStackTrace(System.err);
            System.exit(1);
        }
    }
}
