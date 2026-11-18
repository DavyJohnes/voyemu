package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction11x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21s;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Set;

public final class DonorJarTest {
    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    interface Run {
        void run() throws Exception;
    }

    private static void reject(String code, Run run) throws Exception {
        try {
            run.run();
            throw new AssertionError("accepted " + code);
        } catch (Failure e) {
            check(e.code.equals(code), "unexpected failure " + e.code);
        }
    }

    static ImmutableClassDef type(String name, int value) {
        var method =
                new ImmutableMethod(
                        name,
                        "value",
                        List.of(),
                        "I",
                        9,
                        Set.of(),
                        Set.of(),
                        new ImmutableMethodImplementation(
                                1,
                                List.of(
                                        new ImmutableInstruction21s(Opcode.CONST_16, 0, value),
                                        new ImmutableInstruction11x(Opcode.RETURN, 0)),
                                List.of(),
                                List.of()));

        return new ImmutableClassDef(
                name,
                1,
                "Ljava/lang/Object;",
                List.of(),
                null,
                Set.of(),
                List.of(),
                List.of(),
                List.of(method),
                List.of());
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 2) {
            throw new IllegalArgumentException("ROOT ANDROID_JAR");
        }

        Path project = Path.of(args[0]).toRealPath(),
                platform = Path.of(args[1]).toRealPath(),
                root =
                        Files.createTempDirectory(
                                project.resolve(".local/build/tmp"), "donor-jar-test-");

        try {
            var donor = type("Lfixture/Donor;", 42);
            var nested = type("Lfixture/Donor$Nested;", 7);
            var unselected = type("Lfixture/Other;", 9);
            byte[] source =
                    FrameworkDexMaterializer.serialize(
                            new ImmutableDexFile(
                                    Opcodes.forApi(30), List.of(donor, nested, unselected)));
            Path original = root.resolve("owner.jar");
            NativeCore.zip(original, Map.of("classes.dex", source), false);
            String pin = FilesSupport.hash(original);
            Map<String, Object> recipe =
                    Map.of(
                            "original",
                            "frameworkBase",
                            "sha256",
                            pin,
                            "dex",
                            "classes.dex",
                            "classes",
                            List.of("fixture.Donor", "fixture.Donor$Nested"));
            Path absent = root.resolve("no-own-source"), good = root.resolve("good");
            Path output =
                    DexOperations.donorJar(
                            original, recipe, absent, platform, good, "own-library.jar", 30);
            var entries = NativeCore.entries(output);
            check(
                    entries.keySet().equals(Set.of("classes.dex")),
                    "donor-only JAR contains one exact DEX");
            var actual = new DexBackedDexFile(Opcodes.forApi(30), entries.get("classes.dex"));
            check(
                    actual.getClasses().size() == 2,
                    "finite class allowlist excludes unrelated class");

            for (var type : actual.getClasses()) {
                check(
                        FrameworkDexMaterializer.semantic(type)
                                .equals(
                                        FrameworkDexMaterializer.semantic(
                                                type.getType().equals(donor.getType())
                                                        ? donor
                                                        : nested)),
                        "all donor metadata and executable bodies preserved");
            }

            checkRejectedSpecs(root, platform, original, recipe, absent);
            Path own = root.resolve("own");
            Files.createDirectories(own);
            Files.writeString(
                    own.resolve("Donor.java"), "package fixture; public class Donor {}\n");
            Path collision = root.resolve("collision");
            reject(
                    "DEX_CLASS_COLLISION",
                    () ->
                            DexOperations.donorJar(
                                    original, recipe, own, platform, collision, "x.jar", 30));
            check(
                    !Files.exists(collision.resolve("x.jar")),
                    "own/donor class collision cannot publish library");
            Files.delete(own.resolve("Donor.java"));
            Files.writeString(
                    own.resolve("Own.java"),
                    "package fixture; public class Own { public int value(){return 3;} }\n");
            Path mixed =
                    DexOperations.donorJar(
                            original, recipe, own, platform, root.resolve("mixed"), "x.jar", 30);
            check(
                    NativeCore.entries(mixed)
                            .keySet()
                            .equals(Set.of("classes.dex", "classes2.dex")),
                    "own code and owner donor occupy separate noncolliding slots");
            check(
                    java.util.Arrays.equals(
                            NativeCore.entries(mixed).get("classes.dex"),
                            entries.get("classes.dex")),
                    "own compilation preserves exact donor DEX bytes");
        } finally {
            FilesSupport.removeOwnedTree(root);
        }

        System.out.println(
                "Pinned donor JAR semantics/finite allowlist/API/collision guards passed;"
                        + " guest_started=false");
    }

    private static void checkRejectedSpecs(
            Path root, Path platform, Path original, Map<String, Object> recipe, Path absent)
            throws Exception {
        Map<String, Object> wrong = new java.util.TreeMap<>(recipe);

        wrong.put("sha256", "0".repeat(64));
        Path wrongOutput = root.resolve("wrong");
        reject(
                "DONOR_HASH_MISMATCH",
                () ->
                        DexOperations.donorJar(
                                original, wrong, absent, platform, wrongOutput, "x.jar", 30));
        check(!Files.exists(wrongOutput), "wrong input pin rejects before work creation");
        Map<String, Object> duplicate = new java.util.TreeMap<>(recipe);
        duplicate.put("classes", List.of("fixture.Donor", "fixture.Donor"));
        Path duplicateOutput = root.resolve("duplicate");
        reject(
                "DONOR_SPEC_INVALID",
                () ->
                        DexOperations.donorJar(
                                original,
                                duplicate,
                                absent,
                                platform,
                                duplicateOutput,
                                "x.jar",
                                30));
        check(
                !Files.exists(duplicateOutput),
                "duplicate class request rejects before work creation");
        Map<String, Object> wildcard = new java.util.TreeMap<>(recipe);
        wildcard.put("classes", List.of("fixture.*"));
        reject(
                "DONOR_SPEC_INVALID",
                () ->
                        DexOperations.donorJar(
                                original,
                                wildcard,
                                absent,
                                platform,
                                root.resolve("wildcard"),
                                "x.jar",
                                30));
        Map<String, Object> missingNested = new java.util.TreeMap<>(recipe);
        missingNested.put("classes", List.of("fixture.Donor"));
        Path incomplete = root.resolve("incomplete");
        reject(
                "DONOR_ALLOWLIST_MISMATCH",
                () ->
                        DexOperations.donorJar(
                                original,
                                missingNested,
                                absent,
                                platform,
                                incomplete,
                                "x.jar",
                                30));
        check(
                !Files.exists(incomplete.resolve("x.jar")),
                "implicit nested class cannot widen allowlist or publish JAR");
        Map<String, Object> missing = new java.util.TreeMap<>(recipe);
        missing.put("classes", List.of("fixture.Missing"));
        reject(
                "DONOR_CLASS_MISSING",
                () ->
                        DexOperations.donorJar(
                                original,
                                missing,
                                absent,
                                platform,
                                root.resolve("missing"),
                                "x.jar",
                                30));
        reject(
                "DONOR_API_UNSUPPORTED",
                () ->
                        DexOperations.donorJar(
                                original,
                                recipe,
                                absent,
                                platform,
                                root.resolve("api28"),
                                "x.jar",
                                28));
    }
}
