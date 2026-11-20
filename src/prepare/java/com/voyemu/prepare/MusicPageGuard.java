package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11n;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableStringReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

// The OEM two-page BT/USB branch is required when the optional Kuwo implementation is absent.
final class MusicPageGuard {
    static final String NAME = "api30-music-page";

    static final String OWNER = "Lcom/qinggan/base/manager/AppManager;",
            IMPLEMENTATION = "com.qinggan.app.kwSdkMusic.KwApiImpl";

    static void require(boolean value) {
        Failure.require(
                value, "MUSIC_PAGE_CONTRACT", "Exact optional Music page contract required", 3);
    }

    static boolean target(Method m) {
        return m.getDefiningClass().equals(OWNER)
                && m.getName().equals("isSupportKwMusic")
                && m.getParameterTypes().isEmpty()
                && m.getReturnType().equals("Z");
    }

    static Method rewrite(Map<String, Object> constants, Method m, boolean inverse)
            throws Exception {
        var b = new MutableMethodImplementation(m.getImplementation());

        if (inverse) {
            require(b.getInstructions().size() == 11 && b.getTryBlocks().size() == 1);
            var code =
                    List.of(
                            b.getInstructions().get(0),
                            b.getInstructions().get(1),
                            b.getInstructions().get(3));

            return PassionServiceForegroundGuard.copy(
                    m,
                    new MutableMethodImplementation(
                            new ImmutableMethodImplementation(
                                    1, code, List.of(), b.getDebugItems())));
        }

        require(
                m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == 1
                        && m.getImplementation().getTryBlocks().isEmpty());

        if (StepConstants.flag(constants, "music_page_boundary")) {
            var code = b.getInstructions();
            require(
                    code.size() == 3
                            && code.get(0).getOpcode() == Opcode.INVOKE_STATIC
                            && code.get(0)
                                    instanceof org.jf.dexlib2.iface.instruction.ReferenceInstruction
                            && ((org.jf.dexlib2.iface.instruction.ReferenceInstruction) code.get(0))
                                    .getReference()
                                    .toString()
                                    .equals(
                                            "Lcom/qinggan/media/helper/utils/AppUtils;->isSupportKwMusic()Z")
                            && code.get(1).getOpcode() == Opcode.MOVE_RESULT
                            && code.get(2).getOpcode() == Opcode.RETURN
                            && ((org.jf.dexlib2.iface.instruction.OneRegisterInstruction)
                                                    code.get(1))
                                            .getRegisterA()
                                    == 0
                            && ((org.jf.dexlib2.iface.instruction.OneRegisterInstruction)
                                                    code.get(2))
                                            .getRegisterA()
                                    == 0);
        }

        // Keep the original flag-false return; only a supported page probes the OEM reflection
        // name.
        b.addInstruction(
                new BuilderInstruction21c(
                        Opcode.CONST_STRING, 0, new ImmutableStringReference(IMPLEMENTATION)));
        b.addInstruction(
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        1,
                        0,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Ljava/lang/Class;",
                                "forName",
                                List.of("Ljava/lang/String;"),
                                "Ljava/lang/Class;")));
        b.addInstruction(new BuilderInstruction11n(Opcode.CONST_4, 0, 1));
        b.addInstruction(new BuilderInstruction11x(Opcode.RETURN, 0));
        b.addInstruction(new BuilderInstruction11x(Opcode.MOVE_EXCEPTION, 0));
        b.addInstruction(new BuilderInstruction11n(Opcode.CONST_4, 0, 0));
        b.addInstruction(new BuilderInstruction11x(Opcode.RETURN, 0));
        var probe = b.newLabelForIndex(3);
        var start = b.newLabelForIndex(4);
        var end = b.newLabelForIndex(5);
        var absent = b.newLabelForIndex(7);
        b.addCatch("Ljava/lang/ClassNotFoundException;", start, end, absent);
        b.addInstruction(2, new BuilderInstruction21t(Opcode.IF_NEZ, 0, probe));

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
