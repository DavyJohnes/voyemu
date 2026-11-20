package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.BuilderInstruction;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction12x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21s;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22b;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

final class MediaNotificationGuard {

    static final String NAME = "api30-media-notification-actions",
            OWNER = "Lcom/qinggan/media/base/MediaNotificationManager;";
    static final String COMPACT =
            "Landroidx/media/app/NotificationCompat$MediaStyle;->setShowActionsInCompactView([I)Landroidx/media/app/NotificationCompat$MediaStyle;";

    static void require(boolean b) {
        Failure.require(
                b,
                "MEDIA_NOTIFICATION_CONTRACT",
                "Exact Free A11 7.3.1 OEM media notification actions required",
                3);
    }

    static boolean target(Method m) {
        return m.getDefiningClass().equals(OWNER)
                && m.getName().equals("buildNotification")
                && m.getParameterTypes()
                        .equals(
                                List.of(
                                        "Landroid/support/v4/media/session/PlaybackStateCompat;",
                                        "Landroid/support/v4/media/session/MediaSessionCompat$Token;",
                                        "Z",
                                        "Landroid/support/v4/media/MediaDescriptionCompat;"))
                && m.getReturnType().equals("Landroidx/core/app/NotificationCompat$Builder;");
    }

    static List<BuilderInstruction> block() {
        return List.of(
                new BuilderInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        1,
                        7,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Landroid/support/v4/media/session/PlaybackStateCompat;",
                                "getActions",
                                List.of(),
                                "J")),
                new BuilderInstruction11x(Opcode.MOVE_RESULT_WIDE, 2),
                new BuilderInstruction21s(Opcode.CONST_WIDE_16, 4, 0x30),
                new BuilderInstruction12x(Opcode.AND_LONG_2ADDR, 2, 4),
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        2,
                        2,
                        3,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Ljava/lang/Long;", "bitCount", List.of("J"), "I")),
                new BuilderInstruction11x(Opcode.MOVE_RESULT, 2),
                new BuilderInstruction22b(Opcode.ADD_INT_LIT8, 2, 2, 1),
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        2,
                        1,
                        2,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Ljava/util/Arrays;", "copyOf", List.of("[I", "I"), "[I")),
                new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 1),
                new BuilderInstruction10x(Opcode.NOP));
    }

    static Method rewrite(Method m, boolean inverse) throws Exception {
        require(
                m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == 11
                        && m.getImplementation().getTryBlocks().isEmpty());

        var b = new MutableMethodImplementation(m.getImplementation());
        int at = -1, address = 0;

        for (int i = 0; i < b.getInstructions().size(); i++) {
            var ins = b.getInstructions().get(i);

            if (ins instanceof ReferenceInstruction ri
                    && ri.getReference().toString().equals(COMPACT)) {
                require(at < 0 && address == (inverse ? 51 : 33));
                at = i;
                var r = (FiveRegisterInstruction) ins;
                require(
                        r.getRegisterCount() == 2
                                && r.getRegisterC() == 8
                                && r.getRegisterD() == 1);
            }

            address += ins.getCodeUnits();
        }

        require(at >= 0);

        if (inverse) {
            require(at >= 10);

            for (int i = 0; i < 10; i++) {
                b.removeInstruction(at - 10);
            }
        } else {
            var insert = block();

            for (int i = 0; i < insert.size(); i++) {
                b.addInstruction(at + i, insert.get(i));
            }
        }

        return PassionServiceForegroundGuard.copy(m, b);
    }

    static Map<String, ClassDef> transform(Map<String, ClassDef> input, boolean inverse)
            throws Exception {
        var out = new TreeMap<String, ClassDef>();
        int count = 0;

        for (var c : input.values()) {
            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();
            int gi = 0;

            for (var group : List.of(c.getDirectMethods(), c.getVirtualMethods())) {
                var dest = gi++ == 0 ? direct : virtual;

                for (var m : group) {
                    Method n = m;

                    if (target(m)) {
                        n = rewrite(m, inverse);

                        count++;
                    }

                    dest.add(n);
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

    static void validate(
            Map<String, Object> constants, int api, String abi, Path original, byte[] dex)
            throws Exception {
        require(
                api == 30
                        && abi.equals("arm64-v8a")
                        && StepConstants.admits(constants, FilesSupport.hash(original))
                        && dex != null);
        transform(PassionServiceForegroundGuard.classes(dex), false);
    }

    static byte[] apply(
            Map<String, Object> constants, int api, String abi, Path original, byte[] dex)
            throws Exception {
        validate(constants, api, abi, original, dex);
        var before = PassionServiceForegroundGuard.classes(dex);
        var expected = transform(before, false);
        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), expected.values()));
        var actual = PassionServiceForegroundGuard.classes(output);
        require(actual.keySet().equals(before.keySet()));

        for (var n : expected.keySet()) {
            require(
                    FrameworkDexMaterializer.semantic(expected.get(n))
                            .equals(FrameworkDexMaterializer.semantic(actual.get(n))));
        }

        var restored = transform(actual, true);

        for (var n : before.keySet()) {
            require(
                    FrameworkDexMaterializer.semantic(before.get(n))
                            .equals(FrameworkDexMaterializer.semantic(restored.get(n))));
        }

        return output;
    }
}
