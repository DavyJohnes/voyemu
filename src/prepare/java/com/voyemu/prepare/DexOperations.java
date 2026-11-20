package com.voyemu.prepare;

import com.android.apksig.ApkSigner;
import com.android.apksig.ApkVerifier;
import com.android.tools.r8.CompilationMode;
import com.android.tools.r8.D8;
import com.android.tools.r8.D8Command;
import com.android.tools.r8.OutputMode;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.writer.pool.DexPool;

import java.io.ByteArrayInputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.KeyFactory;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.security.spec.PKCS8EncodedKeySpec;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

final class DexOperations {
    record Donor(Path dex, Path signatures, Set<String> descriptors) {}

    static DexFile dex(Path file, String entry, int api) throws Exception {
        return DexFileFactory.loadDexEntry(file.toFile(), entry, true, Opcodes.forApi(api))
                .getDexFile();
    }

    static Donor donor(Path apk, Map<String, Object> recipe, Path output, int api)
            throws Exception {
        return donor(apk, recipe, output, api, "");
    }

    static Donor donor(Path apk, Map<String, Object> recipe, Path output, int api, String profile)
            throws Exception {
        return donor(apk, recipe, output, api, profile, false);
    }

    /**
     * Exact keeps only the declared classes, as API 28 donor libraries declare every nested class;
     * otherwise the nested classes of each declared class follow it.
     */
    static Donor donor(
            Path apk,
            Map<String, Object> recipe,
            Path output,
            int api,
            String profile,
            boolean exact)
            throws Exception {
        Storage.directory(output);
        DexFile dex = dex(apk, Json.str(recipe, "dex"), api);
        Set<String> roots = new TreeSet<>();

        for (Object name : Json.list(recipe.get("classes"))) {
            Failure.require(
                    name instanceof String, "DONOR_CLASS_INVALID", "Invalid donor class name", 2);
            roots.add("L" + ((String) name).replace('.', '/') + ";");
        }

        Map<String, ClassDef> available = new TreeMap<>();

        for (ClassDef type : dex.getClasses()) {
            available.put(type.getType(), type);
        }

        for (String root : roots) {
            Failure.require(
                    available.containsKey(root),
                    "DONOR_CLASS_MISSING",
                    "Missing declared donor class",
                    3);
        }

        List<ClassDef> selected = new ArrayList<>();
        Set<String> descriptors = new TreeSet<>();

        for (var entry : available.entrySet()) {
            boolean keep = roots.contains(entry.getKey());

            if (!exact) {
                for (String root : roots) {
                    keep |= entry.getKey().startsWith(root.substring(0, root.length() - 1) + "$");
                }
            }

            if (keep) {
                selected.add(entry.getValue());
                descriptors.add(entry.getKey());
            }
        }

        Path payload = output.resolve("donor.dex");
        DexPool.writeTo(payload.toString(), new ImmutableDexFile(Opcodes.forApi(api), selected));

        if (api == 30
                && roots.equals(
                        Set.of(
                                "Lcom/qinggan/power/IPowerManagerListener;",
                                "Lcom/qinggan/system/IPowerManagerService;"))) {
            Failure.require(
                    AudioPolicyConsumerGuard.pins(profile).contains(FilesSupport.hash(apk)),
                    "DONOR_PIN_MISMATCH",
                    "Exact profile-bound OEM power ABI required",
                    5);
            byte[] written = Files.readAllBytes(payload);
            AudioPolicyConsumerGuard.correctWriterHiddenApiClassOffsets(written, descriptors);
            AudioPolicyConsumerGuard.verifyHiddenApiLayout(written);
            Files.write(payload, written);
        }

        Failure.require(
                NativeCore.classes(
                                DexFileFactory.loadDexFile(payload.toFile(), Opcodes.forApi(api)))
                        .equals(descriptors),
                "DONOR_CLASS_INVENTORY_CHANGED",
                "Donor DEX class set changed",
                5);
        Path signature = Signatures.jar(selected, output.resolve("signatures.jar"));

        return new Donor(payload, signature, descriptors);
    }

    static Path donorJar(
            Path original,
            Map<String, Object> recipe,
            Path ownSource,
            Path platform,
            Path work,
            String outputName,
            int api)
            throws Exception {
        return donorJar(
                original,
                recipe,
                ownSource,
                platform,
                work,
                outputName,
                new DonorTarget(api, "", ""));
    }

    /** The Android API, reference profile and optional compatibility adapter of a donor jar. */
    record DonorTarget(int api, String profile, String compatibilityAdapter) {}

    static Path donorJar(
            Path original,
            Map<String, Object> recipe,
            Path ownSource,
            Path platform,
            Path work,
            String outputName,
            DonorTarget target)
            throws Exception {
        int api = target.api();
        String profile = target.profile(), compatibilityAdapter = target.compatibilityAdapter();

        requireCompatibilityAdapter(recipe, api, profile, compatibilityAdapter);
        Failure.require(
                recipe.keySet().equals(Set.of("original", "sha256", "dex", "classes")),
                "DONOR_SPEC_INVALID",
                "Exact donor library fields required",
                2);
        String pin = Json.str(recipe, "sha256");
        Failure.require(
                pin.matches("[0-9a-f]{64}")
                        && Files.isRegularFile(original)
                        && !Files.isSymbolicLink(original)
                        && Files.size(original) <= (128L << 20)
                        && FilesSupport.hash(original).equals(pin),
                "DONOR_HASH_MISMATCH",
                "Owner donor differs from exact library pin",
                3);
        Failure.require(
                Json.str(recipe, "dex").matches("classes(?:[2-9][0-9]*)?\\.dex"),
                "DONOR_SPEC_INVALID",
                "Explicit DEX entry required",
                2);
        List<Object> roots = Json.list(recipe.get("classes"));
        Set<String> expected = donorClasses(roots);
        Failure.require(
                !roots.isEmpty()
                        && (roots.size() <= 64
                                || NxpQgapiLibrary.selected(api, pin, outputName, expected)),
                "DONOR_SPEC_INVALID",
                "Finite donor class allowlist required",
                2);
        Failure.require(
                api == 30
                        || NxpMediaLibrary.selected(api, pin, outputName, expected)
                        || NxpSettingsLibrary.selected(api, pin, outputName, expected)
                        || NxpQgapiLibrary.selected(api, pin, outputName, expected),
                "DONOR_API_UNSUPPORTED",
                "Pinned donor library API and owner scope differ",
                3);
        boolean standalone = PassionStandaloneLibrary.selected(api, pin, outputName, expected);
        Failure.require(
                !Files.exists(work) && outputName.matches("[A-Za-z0-9_.-]+\\.jar"),
                "DONOR_OUTPUT_INVALID",
                "Fresh donor library output required",
                2);
        Storage.directory(work);
        Donor selected = donor(original, recipe, work.resolve("donor"), api, profile, api == 28);
        Failure.require(
                selected.descriptors().equals(expected),
                "DONOR_ALLOWLIST_MISMATCH",
                "Declare every donor class including nested classes",
                3);
        byte[] payload = Files.readAllBytes(selected.dex());

        if (!compatibilityAdapter.isEmpty()) {
            payload = PassionPowerAdapter.apply(profile, api, original, payload);
        }

        if (standalone) {
            payload = PassionStandaloneLibrary.apply(payload, expected);
        }

        Map<String, byte[]> entries = new TreeMap<>();
        entries.put("classes.dex", payload);

        if (Files.isDirectory(ownSource) && !FilesSupport.tree(ownSource).isEmpty()) {
            entries.putAll(ownDonorSlot(ownSource, expected, platform, work, api));
        }

        Failure.require(
                FilesSupport.hash(original).equals(pin),
                "DONOR_HASH_MISMATCH",
                "Owner donor changed during preparation",
                3);
        Path output = work.resolve(outputName);
        NativeCore.zip(output, entries, false);

        return output;
    }

    static void requireCompatibilityAdapter(
            Map<String, Object> recipe, int api, String profile, String compatibilityAdapter) {
        Failure.require(
                compatibilityAdapter.isEmpty()
                        || (compatibilityAdapter.equals(PassionPowerAdapter.NAME)
                                && api == 30
                                && Json.str(recipe, "sha256").equals(PassionPowerAdapter.PIN)
                                && Json.str(recipe, "dex").equals("classes4.dex")),
                "DONOR_COMPATIBILITY_INVALID",
                "Exact Passion power adapter contract required",
                3);
    }

    static Set<String> donorClasses(List<Object> roots) {
        Set<String> expected = new TreeSet<>();

        for (Object root : roots) {
            Failure.require(
                    root instanceof String string
                            && string.matches("[A-Za-z_$][A-Za-z0-9_$.]*")
                            && expected.add("L" + string.replace('.', '/') + ";"),
                    "DONOR_SPEC_INVALID",
                    "Unique explicit donor classes required",
                    2);
        }

        return expected;
    }

    // Own donor-library sources with their compile-only declarations in the second DEX slot.
    static Map<String, byte[]> ownDonorSlot(
            Path ownSource, Set<String> expected, Path platform, Path work, int api)
            throws Exception {
        Path compiled =
                GuestJava.dex(new GuestJava.Unit(ownSource, api), platform, work.resolve("java"));
        Map<String, byte[]> bytes = new TreeMap<>();
        Set<String> actual = new TreeSet<>();

        for (var item : FilesSupport.tree(compiled).entrySet()) {
            String name = item.getKey();
            Failure.require(
                    name.equals("classes.dex"),
                    "DEX_SLOT_COLLISION",
                    "Own donor-library code must occupy one DEX slot",
                    5);

            for (String descriptor :
                    NativeCore.classes(
                            DexFileFactory.loadDexFile(
                                    item.getValue().toFile(), Opcodes.forApi(api)))) {
                Failure.require(
                        actual.add(descriptor) && !expected.contains(descriptor),
                        "DEX_CLASS_COLLISION",
                        "Own and donor library classes collide",
                        5);
            }

            bytes.put("classes2.dex", Files.readAllBytes(item.getValue()));
        }

        return bytes;
    }

    static byte[] donorSlot(Path adapter, Set<String> requested, Path work, int api)
            throws Exception {
        Storage.directory(work);
        var container = DexFileFactory.loadDexContainer(adapter.toFile(), Opcodes.forApi(api));
        Failure.require(
                container.getDexEntryNames().size() >= 1
                        && container.getDexEntryNames().size() <= 2,
                "PATCH_CLASS_MISMATCH",
                "Bounded donor DEX inventory required",
                5);
        List<ClassDef> classes = new ArrayList<>();
        Set<String> actual = new TreeSet<>();

        for (String name : container.getDexEntryNames()) {
            for (var type : container.getEntry(name).getDexFile().getClasses()) {
                Failure.require(
                        actual.add(type.getType()),
                        "DEX_CLASS_COLLISION",
                        "Donor JAR has duplicate classes",
                        5);
                classes.add(type);
            }
        }

        Failure.require(
                actual.equals(requested),
                "PATCH_CLASS_MISMATCH",
                "Exact complete donor JAR class inventory required",
                5);
        Path output = work.resolve("donor.dex");
        DexPool.writeTo(output.toString(), new ImmutableDexFile(Opcodes.forApi(api), classes));
        Failure.require(
                NativeCore.classes(DexFileFactory.loadDexFile(output.toFile(), Opcodes.forApi(api)))
                        .equals(requested),
                "DEX_CLASS_INVENTORY_CHANGED",
                "Embedded donor inventory changed",
                5);

        return Files.readAllBytes(output);
    }

    static Path javaDex(
            Path source, Path generated, List<Path> libraries, Path platform, Path work, int api)
            throws Exception {
        return javaDex(source, generated, libraries, platform, work, api, null);
    }

    static Path javaDex(
            Path source,
            Path generated,
            List<Path> libraries,
            Path platform,
            Path work,
            int api,
            Path compileOnly)
            throws Exception {
        List<Path> sources = new ArrayList<>();
        List<Path> dependencies = new ArrayList<>(libraries);
        Path declarations = compileOnly == null ? null : compileOnly.toAbsolutePath().normalize();

        if (declarations != null) {
            dependencies.add(compileOnlyJar(source, declarations, platform, libraries, work));
        }

        for (Path file : FilesSupport.tree(source).values()) {
            if (file.toString().endsWith(".java")
                    && (declarations == null
                            || !file.toAbsolutePath().normalize().startsWith(declarations))) {
                sources.add(file);
            }
        }

        if (generated != null && Files.exists(generated)) {
            for (Path file : FilesSupport.tree(generated).values()) {
                if (file.toString().endsWith(".java")) {
                    sources.add(file);
                }
            }
        }

        Storage.directory(work);
        Path classes = work.resolve("classes");
        JavaSources.compile(sources, classes, platform, dependencies);
        Map<String, byte[]> classFiles = new TreeMap<>();

        for (var entry : FilesSupport.tree(classes).entrySet()) {
            Failure.require(
                    entry.getKey().endsWith(".class"),
                    "JAVA_OUTPUT_INVALID",
                    "Unexpected compiler output",
                    5);
            classFiles.put(entry.getKey(), Files.readAllBytes(entry.getValue()));
        }

        Path jar = work.resolve("own-classes.jar");
        NativeCore.zip(jar, classFiles, false);
        Path dex = work.resolve("dex");
        Storage.directory(dex);
        var builder =
                D8Command.builder()
                        .addProgramFiles(jar)
                        .addLibraryFiles(platform)
                        .setMode(CompilationMode.RELEASE)
                        .setMinApiLevel(api)
                        .setOutput(dex, OutputMode.DexIndexed);

        for (Path library : dependencies) {
            builder.addLibraryFiles(library);
        }

        D8.run(builder.build());
        Failure.require(
                Files.isRegularFile(dex.resolve("classes.dex")),
                "DEX_OUTPUT_MISSING",
                "D8 produced no primary DEX",
                5);

        return dex;
    }

    static Path compileOnlyJar(
            Path source, Path declarations, Path platform, List<Path> libraries, Path work)
            throws Exception {
        Path root = source.toAbsolutePath().normalize();

        Failure.require(
                !declarations.equals(root)
                        && declarations.startsWith(root)
                        && Files.isDirectory(declarations),
                "JAVA_COMPILE_ONLY_INVALID",
                "Compile-only declarations must be a nested source directory",
                5);
        Storage.confined(root, root.relativize(declarations).toString());
        List<Path> declarationSources = new ArrayList<>();

        for (Path file : FilesSupport.tree(declarations).values()) {
            if (file.toString().endsWith(".java")) {
                declarationSources.add(file);
            }
        }

        Path declarationClasses = work.resolve("compile-only-classes");
        JavaSources.compile(declarationSources, declarationClasses, platform, libraries);
        Map<String, byte[]> declarationFiles = new TreeMap<>();

        for (var entry : FilesSupport.tree(declarationClasses).entrySet()) {
            Failure.require(
                    entry.getKey().endsWith(".class"),
                    "JAVA_OUTPUT_INVALID",
                    "Unexpected compiler output",
                    5);
            declarationFiles.put(entry.getKey(), Files.readAllBytes(entry.getValue()));
        }

        Path declarationJar = work.resolve("compile-only.jar");
        NativeCore.zip(declarationJar, declarationFiles, false);

        return declarationJar;
    }

    static Path sign(Path unsigned, Path signed, byte[] keyBytes, byte[] certificateBytes, int api)
            throws Exception {
        var key = KeyFactory.getInstance("RSA").generatePrivate(new PKCS8EncodedKeySpec(keyBytes));
        var certificate =
                (X509Certificate)
                        CertificateFactory.getInstance("X.509")
                                .generateCertificate(new ByteArrayInputStream(certificateBytes));
        var config =
                new ApkSigner.SignerConfig.Builder("voyemu", key, List.of(certificate)).build();

        new ApkSigner.Builder(List.of(config))
                .setInputApk(unsigned.toFile())
                .setOutputApk(signed.toFile())
                .setAlignmentPreserved(true)
                .setMinSdkVersion(api)
                .setV1SigningEnabled(true)
                .setV2SigningEnabled(true)
                .setV3SigningEnabled(true)
                .setV4SigningEnabled(false)
                .build()
                .sign();
        var verification =
                new ApkVerifier.Builder(signed.toFile())
                        .setMinCheckedPlatformVersion(api)
                        .setMaxCheckedPlatformVersion(api)
                        .build()
                        .verify();
        Failure.require(
                verification.isVerified()
                        && verification.getSignerCertificates().size() == 1
                        && NativeCore.hash(verification.getSignerCertificates().get(0).getEncoded())
                                .equals(NativeCore.hash(certificate.getEncoded())),
                "APK_SIGNATURE_INVALID",
                "Signed APK verification failed",
                5);

        return signed;
    }

    static boolean signatureEntry(String name) {
        return name.toUpperCase(java.util.Locale.ROOT)
                .matches("META-INF/(?:MANIFEST\\.MF|.*\\.(?:SF|RSA|DSA|EC))");
    }
}
