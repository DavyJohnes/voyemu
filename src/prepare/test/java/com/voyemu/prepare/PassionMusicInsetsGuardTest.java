package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21s;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableMethod;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Real pinned OEM DEX regression; Android layout and input acceptance remain runtime work. */
public final class PassionMusicInsetsGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static void check(boolean ok, String message) {
        if (!ok) {
            throw new AssertionError(message);
        }
    }

    static void reject(Action action) throws Exception {
        boolean denied = false;

        try {
            action.run();
        } catch (Failure failure) {
            check(failure.getMessage().contains("Exact"), "Contract failure diagnostic");
            denied = true;
        }

        check(denied, "Expected finite guard rejection");
    }

    static DexBackedDexFile dex(byte[] bytes) {
        return new DexBackedDexFile(Opcodes.forApi(30), bytes);
    }

    static Map<String, ClassDef> classes(DexFile dex) {
        var out = new TreeMap<String, ClassDef>();

        for (var c : dex.getClasses()) {
            out.put(c.getType(), c);
        }

        return out;
    }

    static Method copy(Method m, MutableMethodImplementation b) {
        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                b);
    }

    static Method mutatedConstant(Method m, int register, int value) {
        var b = new MutableMethodImplementation(m.getImplementation());
        int count = 0;

        for (int i = 0; i < b.getInstructions().size(); i++) {
            var old = b.getInstructions().get(i);

            if (old.getOpcode() == Opcode.CONST_16
                    && ((NarrowLiteralInstruction) old).getNarrowLiteral()
                            == PassionMusicInsetsGuard.FIT_VISIBILITY) {
                b.replaceInstruction(
                        i, new BuilderInstruction21s(Opcode.CONST_16, register, value));
                count++;
            }
        }

        check(count == 1, "One local visibility override");

        return copy(m, b);
    }

    static int helperVisibility(DexFile dex) {
        for (var c : dex.getClasses()) {
            if (c.getType().equals("Lcom/voyah/baselibrary/utils/WindowScreenUtil;")) {
                for (var m : c.getDirectMethods()) {
                    if (m.getName().equals("setTransparentImmersive")) {
                        int constant = -1;

                        for (var i : m.getImplementation().getInstructions()) {
                            if (i.getOpcode() == Opcode.CONST_16) {
                                constant = ((NarrowLiteralInstruction) i).getNarrowLiteral();
                            }

                            if (i instanceof ReferenceInstruction r
                                    && r.getReference()
                                            .toString()
                                            .equals(PassionMusicInsetsGuard.VISIBILITY)) {
                                return constant;
                            }
                        }
                    }
                }
            }
        }

        throw new AssertionError("Original helper missing");
    }

    static int visibilityAtContent(DexFile dex) {
        int visibility = -1, constant = -1;

        for (var i : PassionMusicInsetsGuard.method(dex).getImplementation().getInstructions()) {
            if (i.getOpcode() == Opcode.CONST_16) {
                constant = ((NarrowLiteralInstruction) i).getNarrowLiteral();
            }

            if (i instanceof ReferenceInstruction r) {
                String reference = r.getReference().toString();

                if (reference.equals(PassionMusicInsetsGuard.IMMERSIVE)) {
                    visibility = helperVisibility(dex);
                }

                if (reference.equals(PassionMusicInsetsGuard.VISIBILITY)) {
                    visibility = constant;
                }

                if (reference.startsWith(
                        "Landroidx/databinding/DataBindingUtil;->setContentView(")) {
                    return visibility;
                }
            }
        }

        throw new AssertionError("Original content inflation missing");
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]),
                stage = Path.of(args[1]),
                original =
                        root.resolve(
                                ".local/profiles/"
                                        + PassionMusicInsetsGuard.PROFILE
                                        + "/originals/LY_Multimedia.apk");
        var originalEntries = NativeCore.entries(original);
        byte[] raw = originalEntries.get("classes2.dex");
        byte[] prepared =
                PassionMusicForegroundGuard.apply(
                        PassionMusicInsetsGuard.PROFILE, 30, original, raw);
        var source = dex(prepared);

        check(helperVisibility(source) == 0x700, "Pinned shared helper original 0x700");

        if (args.length > 2 && args[2].equals("--red")) {
            check(
                    (visibilityAtContent(source) & 0x400) == 0,
                    "RED: prepared OEM MainActivity still draws content under the status bar"
                            + " (visibility 0x700)");

            return;
        }

        Files.createDirectory(stage);
        check(visibilityAtContent(source) == 0x700, "Regression exists in actual preguard input");
        PassionMusicInsetsGuard.validateOriginalChain(
                PassionMusicInsetsGuard.PROFILE, 30, original, raw);
        PassionMusicInsetsGuard.validate(PassionMusicInsetsGuard.PROFILE, 30, original, prepared);
        var before = PassionMusicInsetsGuard.method(source);
        PassionMusicInsetsGuard.validateMethod(before);
        reject(() -> PassionMusicInsetsGuard.validateAfter(before));
        byte[] out =
                PassionMusicInsetsGuard.apply(
                        PassionMusicInsetsGuard.PROFILE, 30, original, prepared);
        var actual = dex(out);
        var after = PassionMusicInsetsGuard.method(actual);
        PassionMusicInsetsGuard.validateAfter(after);
        check(
                visibilityAtContent(actual) == 0x300,
                "Only status-bar layout bit cleared before original content inflation");
        check(helperVisibility(actual) == 0x700, "Shared helper still original");
        checkClassScope(source, actual);
        PassionMusicForegroundGuard.validateAfter(PassionMusicForegroundGuard.method(actual));
        PassionMusicForegroundGuard.validateBridgeAfter(
                PassionMusicForegroundGuard.method(actual, PassionMusicForegroundGuard.BRIDGE));
        rejectWrongInputs(original, stage, raw, prepared, out);
        checkAfterMutations(before, after);
        checkChainedEntries(originalEntries, original, prepared, out);
        Path output = stage.resolve("classes2.dex");
        Files.write(output, out);
        Process process =
                new ProcessBuilder(
                                root.resolve(
                                                ".local/toolchains/native-sdk/build-tools/36.1.0/dexdump")
                                        .toString(),
                                "-c",
                                output.toString())
                        .redirectErrorStream(true)
                        .redirectOutput(stage.resolve("dexdump-checksum.log").toFile())
                        .start();
        check(process.waitFor() == 0, "DEX checksum verification");
        Files.writeString(
                stage.resolve("receipt.json"),
                Json.write(
                                Map.of(
                                        "original_layout_fullscreen_flag_RED",
                                        true,
                                        "main_activity_visibility_after",
                                        visibilityAtContent(actual),
                                        "shared_helper_and_Search_exact",
                                        true,
                                        "inverse_all_classes_exact",
                                        true,
                                        "prior_foreground_and_role_preserved",
                                        true,
                                        "negative_cases",
                                        12,
                                        "dexdump_checksum_exit",
                                        0,
                                        "output_dex_sha256",
                                        NativeCore.hash(out),
                                        "runtime_layout_and_input_verified",
                                        false))
                        + "\n");
        System.out.println(
                "Music MainActivity local OEM night before super and exact insets 0x700->0x300, 13"
                        + " negatives, shared helper/Search unchanged, inverse all classes and"
                        + " predecessor guards preserved; checksum PASS; runtime pending");
    }

    static void checkClassScope(DexFile source, DexFile actual) throws Exception {
        var oldClasses = classes(source);
        var newClasses = classes(actual);

        check(oldClasses.keySet().equals(newClasses.keySet()), "No added/removed classes");
        int changed = 0;

        for (var name : oldClasses.keySet()) {
            if (!FrameworkDexMaterializer.semantic(oldClasses.get(name))
                    .equals(FrameworkDexMaterializer.semantic(newClasses.get(name)))) {
                check(
                        name.equals(PassionMusicInsetsGuard.OWNER),
                        "Only Music MainActivity may change: " + name);
                changed++;
            }
        }

        check(changed == 1, "Exactly one changed class");

        for (var name :
                List.of(
                        "Lcom/voyah/baselibrary/utils/WindowScreenUtil;",
                        "Lcom/voyah/multimedia/search/view/SearchActivity;")) {
            check(
                    FrameworkDexMaterializer.semantic(oldClasses.get(name))
                            .equals(FrameworkDexMaterializer.semantic(newClasses.get(name))),
                    "Shared helper/Search remain exact");
        }

        check(
                FrameworkDexMaterializer.semantic(source.getClasses())
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        PassionMusicInsetsGuard.rewrite(actual, true)
                                                .getClasses())),
                "Inverse restores all classes, debug info, branches and members");
    }

    static void rejectWrongInputs(
            Path original, Path stage, byte[] raw, byte[] prepared, byte[] out) throws Exception {
        reject(() -> PassionMusicInsetsGuard.apply("other", 30, original, prepared));
        reject(() ->
                PassionMusicInsetsGuard.apply(
                        PassionMusicInsetsGuard.PROFILE, 29, original, prepared));
        reject(() ->
                PassionMusicInsetsGuard.apply(PassionMusicInsetsGuard.PROFILE, 30, original, null));
        reject(() ->
                PassionMusicInsetsGuard.apply(PassionMusicInsetsGuard.PROFILE, 30, original, out));
        Path wrong = stage.resolve("wrong.apk");
        Files.writeString(wrong, "wrong original");
        reject(() ->
                PassionMusicInsetsGuard.apply(
                        PassionMusicInsetsGuard.PROFILE, 30, wrong, prepared));
    }

    static void checkAfterMutations(Method before, Method after) throws Exception {
        var nightCode = new MutableMethodImplementation(after.getImplementation());

        check(
                nightCode.getInstructions().get(0) instanceof ReferenceInstruction r
                        && r.getReference().toString().equals(PassionMusicInsetsGuard.DELEGATE),
                "OEM local delegate precedes super/inflation");
        check(
                nightCode.getInstructions().get(2).getOpcode() == Opcode.CONST_4
                        && ((NarrowLiteralInstruction) nightCode.getInstructions().get(2))
                                        .getNarrowLiteral()
                                == 2,
                "OEM local night mode required");
        nightCode.replaceInstruction(
                2,
                new org.jf.dexlib2.builder.instruction.BuilderInstruction11n(Opcode.CONST_4, 1, 1));
        reject(() -> PassionMusicInsetsGuard.validateAfter(copy(after, nightCode)));
        reject(() -> PassionMusicInsetsGuard.validateAfter(mutatedConstant(after, 1, 0x100)));
        reject(() -> PassionMusicInsetsGuard.validateAfter(mutatedConstant(after, 0, 0x300)));
        var wrongFlags =
                new ImmutableMethod(
                        before.getDefiningClass(),
                        before.getName(),
                        before.getParameters(),
                        before.getReturnType(),
                        1,
                        before.getAnnotations(),
                        before.getHiddenApiRestrictions(),
                        before.getImplementation());
        reject(() -> PassionMusicInsetsGuard.validateMethod(wrongFlags));
    }

    static void checkChainedEntries(
            Map<String, byte[]> originalEntries, Path original, byte[] prepared, byte[] out)
            throws Exception {
        var chained = new TreeMap<String, byte[]>(originalEntries);

        chained.put(
                "classes.dex",
                PassionBluetoothRoleGuard.apply(
                        PassionBluetoothRoleGuard.PROFILE,
                        30,
                        original,
                        chained.get("classes.dex")));
        chained.put("classes2.dex", prepared);
        var prior = new TreeMap<String, byte[]>(chained);
        chained.put("classes2.dex", out);

        for (var entry : prior.entrySet()) {
            if (!entry.getKey().equals("classes2.dex")) {
                check(
                        Arrays.equals(entry.getValue(), chained.get(entry.getKey())),
                        "Prior role and unrelated APK entries preserved");
            }
        }
    }
}
