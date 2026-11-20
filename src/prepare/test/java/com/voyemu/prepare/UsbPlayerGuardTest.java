package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.zip.ZipFile;

public final class UsbPlayerGuardTest {

    interface Action {
        void run() throws Exception;
    }

    static void reject(Action action) throws Exception {
        boolean rejected = false;

        try {
            action.run();
        } catch (Failure expected) {
            rejected = true;
        }

        if (!rejected) {
            throw new AssertionError("Exact pin/chain failure was accepted");
        }
    }

    static Map<String, ClassDef> classes(byte[] dex) {
        var out = new TreeMap<String, ClassDef>();

        for (var type : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            out.put(type.getType(), type);
        }

        return out;
    }

    /** A pin of the stub original; the recipe allowlist declares it. */
    static final String PIN = "ab".repeat(32);

    static final class Target implements Recipes.Target {
        int validations, mutations;

        @Override
        public String profile() {
            return TestProfiles.SYNTHETIC;
        }

        @Override
        public int api() {
            return 30;
        }

        @Override
        public String abi() {
            return "arm64-v8a";
        }

        @Override
        public Path work() {
            return null;
        }

        @Override
        public Path output() {
            return null;
        }

        @Override
        public Map<String, Path> uses() {
            return Map.of();
        }

        @Override
        public String originalSha256() {
            return PIN;
        }

        @Override
        public void addDex(String x) {
            throw new AssertionError();
        }

        @Override
        public void putEntry(Path p, String n) {
            throw new AssertionError();
        }

        @Override
        public void removeEntry(String n) {
            throw new AssertionError();
        }

        @Override
        public void useLibraries(List<String> names) {
            throw new AssertionError();
        }

        @Override
        public void guardUsbPlayer(boolean apply) {
            if (apply) {
                mutations++;
            } else {
                validations++;
            }
        }
    }

    static Map<String, Object> recipe(Object... calls) {
        return Map.of(
                "api",
                30,
                "sourceSha256",
                "a".repeat(64),
                "constants",
                Map.of("original_sha256_allowlist", List.of(PIN)),
                "calls",
                List.of(calls));
    }

    static org.jf.dexlib2.iface.Method synthetic(
            List<org.jf.dexlib2.immutable.ImmutableTryBlock> tries, boolean changeInstruction) {
        var code = new java.util.ArrayList<org.jf.dexlib2.iface.instruction.Instruction>();

        for (int i = 0; i < 6; i++) {
            code.add(
                    changeInstruction && i == 1
                            ? new org.jf.dexlib2.immutable.instruction.ImmutableInstruction11n(
                                    org.jf.dexlib2.Opcode.CONST_4, 0, 1)
                            : new org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x(
                                    org.jf.dexlib2.Opcode.NOP));
        }

        code.add(
                new org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x(
                        org.jf.dexlib2.Opcode.RETURN_VOID));

        return new org.jf.dexlib2.immutable.ImmutableMethod(
                "Lcom/voyemu/test/TryFixture;",
                "run",
                List.of(),
                "V",
                9,
                Set.of(),
                Set.of(),
                new org.jf.dexlib2.immutable.ImmutableMethodImplementation(
                        1, code, tries, List.of()));
    }

    static String canonicalSemantic(org.jf.dexlib2.iface.Method method) throws Exception {
        return FrameworkDexMaterializer.semantic(UsbPlayerGuard.canonicalMethod(method));
    }

    static void normalizationTests() throws Exception {
        var handler =
                new org.jf.dexlib2.immutable.ImmutableExceptionHandler("Ljava/lang/Exception;", 6);
        var alternate =
                new org.jf.dexlib2.immutable.ImmutableExceptionHandler("Ljava/io/IOException;", 5);
        var merged =
                List.of(new org.jf.dexlib2.immutable.ImmutableTryBlock(0, 2, List.of(handler)));
        var split =
                List.of(
                        new org.jf.dexlib2.immutable.ImmutableTryBlock(0, 1, List.of(handler)),
                        new org.jf.dexlib2.immutable.ImmutableTryBlock(1, 1, List.of(handler)));
        String baseline = canonicalSemantic(synthetic(merged, false));

        if (!baseline.equals(canonicalSemantic(synthetic(split, false)))) {
            throw new AssertionError("Equivalent adjacent handler split rejected");
        }

        for (var changed :
                List.of(
                        List.of(
                                new org.jf.dexlib2.immutable.ImmutableTryBlock(
                                        0, 1, List.of(handler))),
                        List.of(
                                new org.jf.dexlib2.immutable.ImmutableTryBlock(
                                        1, 2, List.of(handler))),
                        List.of(
                                new org.jf.dexlib2.immutable.ImmutableTryBlock(
                                        0,
                                        2,
                                        List.of(
                                                new org.jf.dexlib2.immutable
                                                        .ImmutableExceptionHandler(
                                                        "Ljava/lang/Exception;", 5)))),
                        List.of(
                                new org.jf.dexlib2.immutable.ImmutableTryBlock(
                                        0,
                                        2,
                                        List.of(
                                                new org.jf.dexlib2.immutable
                                                        .ImmutableExceptionHandler(
                                                        "Ljava/lang/Throwable;", 6)))))) {
            if (baseline.equals(canonicalSemantic(synthetic(changed, false)))) {
                throw new AssertionError("Changed handler address/type/coverage was hidden");
            }
        }

        var ordered =
                List.of(
                        new org.jf.dexlib2.immutable.ImmutableTryBlock(
                                0, 2, List.of(alternate, handler)));
        var reverse =
                List.of(
                        new org.jf.dexlib2.immutable.ImmutableTryBlock(
                                0, 2, List.of(handler, alternate)));

        if (canonicalSemantic(synthetic(ordered, false))
                .equals(canonicalSemantic(synthetic(reverse, false)))) {
            throw new AssertionError("Changed catch priority was hidden");
        }

        if (baseline.equals(canonicalSemantic(synthetic(merged, true)))) {
            throw new AssertionError("Changed instruction was hidden");
        }

        System.out.println(
                "PASS exact try-split normalization; handler"
                        + " priority/type/address/coverage/instruction negatives");
    }

    public static void main(String[] args) throws Exception {
        normalizationTests();
        Path apk = Path.of(args[0]),
                root = Path.of(args[1]),
                platform = Path.of(args[2]),
                work = Path.of(args[3]);
        Map<String, Object> constants = TestProfiles.constants(root, UsbPlayerGuard.NAME, apk);
        byte[] original;

        try (var zip = new ZipFile(apk.toFile())) {
            original = zip.getInputStream(zip.getEntry("classes3.dex")).readAllBytes();
        }

        byte[] prepared = MediaCloudConfigGuard.apply(constants, 30, "arm64-v8a", apk, original);
        var before = classes(prepared);

        if (!FrameworkDexMaterializer.semantic(before.get(UsbPlayerGuard.PLAYER + ";"))
                .equals(
                        FrameworkDexMaterializer.semantic(
                                classes(original).get(UsbPlayerGuard.PLAYER + ";")))) {
            throw new AssertionError("Cloud patch altered USB player");
        }

        byte[] patched = UsbPlayerGuard.apply(constants, 30, "arm64-v8a", apk, prepared);
        var after = classes(patched);

        if (!before.keySet().equals(after.keySet())) {
            throw new AssertionError("Class inventory changed");
        }

        int changes = changedTargets(before, after);

        for (var field : after.get(UsbPlayerGuard.PLAYER + ";").getFields()) {
            if (field.getName().equals("mMediaPlayer")
                    && !field.getType().equals(UsbPlayerGuard.HELPER)) {
                throw new AssertionError("Original native decoder retained");
            }
        }

        reject(() ->
                UsbPlayerGuard.apply(
                        Map.of(StepConstants.PINS, List.of("0".repeat(64))),
                        30,
                        "arm64-v8a",
                        apk,
                        prepared));
        reject(() -> UsbPlayerGuard.apply(constants, 28, "arm64-v8a", apk, prepared));
        reject(() -> UsbPlayerGuard.apply(constants, 30, "armeabi-v7a", apk, prepared));
        reject(() -> UsbPlayerGuard.apply(constants, 30, "arm64-v8a", apk, patched));
        checkRecipeDispatch();
        Path source =
                root.resolve(
                        "profiles/"
                                + TestProfiles.pinning(root, FilesSupport.hash(apk))
                                + "/patches/"
                                + UsbPlayerGuard.NAME
                                + "/src");
        Path helper =
                UsbPlayerGuard.compileHelper(
                        constants, 30, "arm64-v8a", apk, source, platform, work);
        var helperClasses = classes(Files.readAllBytes(helper));

        if (!helperClasses.containsKey(UsbPlayerGuard.HELPER)
                || helperClasses.keySet().stream().anyMatch(n -> n.startsWith("Lorg/ijkplayer/"))) {
            throw new AssertionError("Compile-only OEM declarations leaked");
        }

        if (!helperClasses
                .get(UsbPlayerGuard.HELPER)
                .getSuperclass()
                .equals("Lorg/ijkplayer/AbstractMediaPlayer;")) {
            throw new AssertionError("OEM callback interface lost");
        }

        System.out.println(
                "PASS USB original native decoder RED -> platform backend references GREEN; inverse"
                        + " all-class semantics; exact chain negatives; recipe dispatch; helper"
                        + " compile-only declarations excluded; changed classes="
                        + changes
                        + " helper="
                        + NativeCore.hash(Files.readAllBytes(helper)));
    }

    static int changedTargets(Map<String, ClassDef> before, Map<String, ClassDef> after)
            throws Exception {
        int changes = 0;

        for (var name : before.keySet()) {
            boolean same =
                    FrameworkDexMaterializer.semantic(
                                    UsbPlayerGuard.canonicalClass(before.get(name)))
                            .equals(
                                    FrameworkDexMaterializer.semantic(
                                            UsbPlayerGuard.canonicalClass(after.get(name))));

            if (!same) {
                if (!UsbPlayerGuard.TARGETS.contains(name)) {
                    throw new AssertionError("Unrelated class changed: " + name);
                }

                changes++;
            }
        }

        if (changes == 0) {
            throw new AssertionError("RED original native player references survived");
        }

        return changes;
    }

    static void checkRecipeDispatch() throws Exception {
        Object call = Map.of("operation", "guardUsbPlayer", "args", List.of());
        var target = new Target();

        Recipes.validateApi30(UsbPlayerGuard.NAME + ".json", recipe(call), target);

        if (target.mutations != 0 || target.validations != 1) {
            throw new AssertionError("Validation mutated target");
        }

        Recipes.apply(UsbPlayerGuard.NAME + ".json", recipe(call), target);

        if (target.mutations != 1 || target.validations != 2) {
            throw new AssertionError("Recipe dispatch absent");
        }

        for (var bad :
                List.of(
                        recipe(call, call),
                        recipe(Map.of("operation", "guardUsbPlayer", "args", List.of("extra"))),
                        recipe(Map.of("operation", "unknown", "args", List.of())))) {
            var t = new Target();
            reject(() -> Recipes.apply(UsbPlayerGuard.NAME + ".json", bad, t));

            if (t.mutations != 0) {
                throw new AssertionError("Invalid recipe mutated APK");
            }
        }
    }
}
