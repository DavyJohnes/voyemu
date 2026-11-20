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

    static final String OWNER = "Lcom/qinggan/app/music/activity/MusicActivity;";

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

    static Method rewrite(Map<String, Object> constants, Method m, boolean inverse)
            throws Exception {
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

            if (StepConstants.flag(constants, "music_fullscreen_boundary")) {
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
            Map<String, Object> constants, Map<String, ClassDef> input, boolean inverse)
            throws Exception {
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
                        next = rewrite(constants, m, inverse);

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

    static void validate(Map<String, Object> constants, int api, Path original, byte[] dex)
            throws Exception {
        require(
                api == 30
                        && StepConstants.admits(constants, FilesSupport.hash(original))
                        && dex != null);
        transform(constants, classes(dex), false);
    }

    static void validateOriginalChain(
            Map<String, Object> constants, int api, Path original, byte[] raw) throws Exception {
        validate(constants, api, original, MusicPageGuard.apply(constants, api, original, raw));
    }

    static byte[] apply(Map<String, Object> constants, int api, Path original, byte[] dex)
            throws Exception {
        validate(constants, api, original, dex);
        var before = classes(dex);
        var expected = transform(constants, before, false);
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

        var restored = transform(constants, actual, true);

        for (var name : before.keySet()) {
            require(
                    FrameworkDexMaterializer.semantic(before.get(name))
                            .equals(FrameworkDexMaterializer.semantic(restored.get(name))));
        }

        return output;
    }
}
