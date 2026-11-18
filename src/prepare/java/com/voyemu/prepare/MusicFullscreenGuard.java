package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21s;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

// PRIVATE PROPOSAL: Public API30 fullscreen for the genuine MusicActivity only.
final class MusicFullscreenGuard {
    static final String NAME = "api30-music-fullscreen";

    /** Pin of the Music input; boundary selects the instruction-boundary check. */
    record Variant(String pin, boolean boundary) {}

    static final Map<String, Variant> VARIANTS =
            Map.of(
                    "free-11-chinese-20250710-6.11.1",
                    new Variant(
                            "5c4d5470b552c69575d5ea285220249ef2ec54ba79160bc6d004199232b0c389",
                            true),
                    "free-11-chinese-20260731-7.3.1",
                    new Variant(
                            "3dfa09d39638888de1f79d679867d1ac57c9a165a0eed6d43b39aedc4be1ea33",
                            false));
    static final String OWNER = "Lcom/qinggan/app/music/activity/MusicActivity;";

    static List<String> pins(String profile) {
        var variant = VARIANTS.get(profile);

        return variant == null ? List.of() : List.of(variant.pin());
    }

    static void require(boolean value) {
        Failure.require(
                value, "MUSIC_FULLSCREEN_CONTRACT", "Exact Music fullscreen contract required", 3);
    }

    static boolean target(Method m) {
        return m.getDefiningClass().equals(OWNER)
                && m.getName().equals("onCreate")
                && m.getParameterTypes().equals(List.of("Landroid/os/Bundle;"))
                && m.getReturnType().equals("V");
    }

    static Method rewrite(String profile, Method m, boolean inverse) throws Exception {
        var b = new MutableMethodImplementation(m.getImplementation());

        if (inverse) {
            for (int i = 0; i < 4; i++) {
                b.removeInstruction(1);
            }
        } else {
            require(
                    m.getImplementation() != null
                            && m.getImplementation().getRegisterCount() == 4
                            && m.getImplementation().getTryBlocks().isEmpty());

            if (VARIANTS.get(profile).boundary()) {
                var code = b.getInstructions();
                require(
                        code.size() > 1
                                && code.get(0).getOpcode() == Opcode.INVOKE_SUPER
                                && code.get(0)
                                        instanceof
                                        org.jf.dexlib2.iface.instruction.ReferenceInstruction
                                && ((org.jf.dexlib2.iface.instruction.ReferenceInstruction)
                                                code.get(0))
                                        .getReference()
                                        .toString()
                                        .equals(
                                                "Lcom/qinggan/base/BaseActivity;->onCreate(Landroid/os/Bundle;)V")
                                && code.get(1).getOpcode() == Opcode.NEW_INSTANCE);
            }

            // v0/v1 are dead after the original superclass call; OEM initialization overwrites
            // both.
            b.addInstruction(
                    1,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_VIRTUAL,
                            1,
                            2,
                            0,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    "Landroid/app/Activity;",
                                    "getWindow",
                                    List.of(),
                                    "Landroid/view/Window;")));
            b.addInstruction(2, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0));
            b.addInstruction(3, new BuilderInstruction21s(Opcode.CONST_16, 1, 1024));
            b.addInstruction(
                    4,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_VIRTUAL,
                            2,
                            0,
                            1,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    "Landroid/view/Window;", "addFlags", List.of("I"), "V")));
        }

        return PassionServiceForegroundGuard.copy(m, b);
    }

    static Map<String, ClassDef> transform(
            String profile, Map<String, ClassDef> input, boolean inverse) throws Exception {
        var out = new TreeMap<String, ClassDef>();
        int count = 0;

        for (var c : input.values()) {
            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();
            int groupIndex = 0;

            for (var group : List.of(c.getDirectMethods(), c.getVirtualMethods())) {
                var destination = groupIndex++ == 0 ? direct : virtual;

                for (var m : group) {
                    Method next = m;

                    if (target(m)) {
                        next = rewrite(profile, m, inverse);

                        count++;
                    }

                    destination.add(next);
                }
            }

            out.put(
                    c.getType(),
                    new ImmutableClassDef(
                            c.getType(),
                            c.getAccessFlags(),
                            c.getSuperclass(),
                            c.getInterfaces(),
                            c.getSourceFile(),
                            c.getAnnotations(),
                            c.getStaticFields(),
                            c.getInstanceFields(),
                            direct,
                            virtual));
        }

        require(count == 1);

        return out;
    }

    static Map<String, ClassDef> classes(byte[] dex) {
        return PassionServiceForegroundGuard.classes(dex);
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        var variant = VARIANTS.get(profile);

        require(
                variant != null
                        && api == 30
                        && variant.pin().equals(FilesSupport.hash(original))
                        && dex != null);
        transform(profile, classes(dex), false);
    }

    static void validateOriginalChain(String profile, int api, Path original, byte[] raw)
            throws Exception {
        validate(profile, api, original, MusicPageGuard.apply(profile, api, original, raw));
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        validate(profile, api, original, dex);
        var before = classes(dex);
        var expected = transform(profile, before, false);
        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), expected.values()));
        var actual = classes(output);
        require(actual.keySet().equals(before.keySet()));

        for (var name : expected.keySet()) {
            require(
                    FrameworkDexMaterializer.semantic(expected.get(name))
                            .equals(FrameworkDexMaterializer.semantic(actual.get(name))));
        }

        var restored = transform(profile, actual, true);

        for (var name : before.keySet()) {
            require(
                    FrameworkDexMaterializer.semantic(before.get(name))
                            .equals(FrameworkDexMaterializer.semantic(restored.get(name))));
        }

        return output;
    }
}
