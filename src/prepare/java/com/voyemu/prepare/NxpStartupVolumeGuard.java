package com.voyemu.prepare;

import org.jf.dexlib2.AccessFlags;
import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderArrayPayload;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11n;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22b;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction31i;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.formats.ArrayPayload;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.reference.ImmutableFieldReference;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/**
 * Exact shared SDK API28 initialization patch for A9 and NXP; later volume operations remain stock.
 */
final class NxpStartupVolumeGuard {
    static final String AUDIO = "Lcom/android/server/audio/AudioService;",
            STREAM = "Lcom/android/server/audio/AudioService$VolumeStreamState;",
            DEFAULTS = "voyemuZeroStartupDefaults",
            RESET = "voyemuResetStartupVolumes";
    static final List<Integer> MIN = List.of(1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1);

    static void need(boolean b) {
        Failure.require(
                b,
                "NXP_STARTUP_AUDIO_CONTRACT",
                "Exact pinned NXP API28 startup audio required",
                3);
    }

    static boolean target(Method m) {
        return (m.getDefiningClass().equals(AUDIO)
                        && (m.getName().equals("<clinit>")
                                || (m.getName().equals("<init>")
                                        && m.getParameterTypes().size() == 1)))
                || (m.getDefiningClass().equals(STREAM)
                        && m.getName().equals("<init>")
                        && m.getParameterTypes().size() == 3);
    }

    static Method rewrite(Method m, boolean inverse) throws Exception {
        if (!target(m)) {
            return m;
        }

        var b = new MutableMethodImplementation(m.getImplementation());
        int found;

        if (m.getName().equals("<clinit>")) {
            found = rewriteVolumeTable(b, inverse);
        } else {
            found = rewriteVolumeReset(b, m.getDefiningClass().equals(STREAM), inverse);
        }

        need(found == 1);

        return StartupVolumeGuard.copy(m, b);
    }

    // Swaps the minimum startup volume table with an all-zero table.
    static int rewriteVolumeTable(MutableMethodImplementation b, boolean inverse) {
        int found = 0;

        for (int i = 0; i < b.getInstructions().size(); i++) {
            if (b.getInstructions().get(i) instanceof ArrayPayload a
                    && a.getArrayElements().stream()
                            .map(Number::intValue)
                            .toList()
                            .equals(inverse ? Collections.nCopies(11, 0) : MIN)) {
                need(a.getElementWidth() == 4);
                b.replaceInstruction(
                        i,
                        new BuilderArrayPayload(
                                4,
                                new ArrayList<Number>(inverse ? MIN : Collections.nCopies(11, 0))));
                found++;
            }
        }

        return found;
    }

    // Inserts or removes the volume reset call next to the settings read or thread start.
    static int rewriteVolumeReset(MutableMethodImplementation b, boolean stream, boolean inverse) {
        int address = 0;

        for (int i = 0; i < b.getInstructions().size(); i++) {
            var ins = b.getInstructions().get(i);
            String ref = ins instanceof ReferenceInstruction r ? r.getReference().toString() : "";

            if (ref.equals(
                    stream
                            ? STREAM + "->readSettings()V"
                            : AUDIO + "->createAudioSystemThread()V")) {
                need(
                        address == (stream ? 45 : inverse ? 605 : 601)
                                && b.getRegisterCount() == (stream ? 7 : 26));

                if (inverse) {
                    b.removeInstruction(stream ? i + 1 : i - 2);
                    b.removeInstruction(stream ? i + 1 : i - 2);
                } else {
                    b.addInstruction(stream ? i + 1 : i, new BuilderInstruction10x(Opcode.NOP));
                    b.addInstruction(
                            stream ? i + 1 : i,
                            stream
                                    ? new BuilderInstruction35c(
                                            Opcode.INVOKE_DIRECT,
                                            1,
                                            3,
                                            0,
                                            0,
                                            0,
                                            0,
                                            new ImmutableMethodReference(
                                                    STREAM, RESET, List.of(), "V"))
                                    : new BuilderInstruction35c(
                                            Opcode.INVOKE_STATIC,
                                            0,
                                            0,
                                            0,
                                            0,
                                            0,
                                            0,
                                            new ImmutableMethodReference(
                                                    AUDIO, DEFAULTS, List.of(), "V")));
                }

                return 1;
            }

            address += ins.getCodeUnits();
        }

        return 0;
    }

    static Method defaults() {
        var instructions =
                List.<Instruction>of(
                        new BuilderInstruction21c(
                                Opcode.SGET_OBJECT,
                                0,
                                new ImmutableFieldReference(
                                        "Landroid/media/AudioSystem;",
                                        "DEFAULT_STREAM_VOLUME",
                                        "[I")),
                        new BuilderInstruction11n(Opcode.CONST_4, 1, 0),
                        new BuilderInstruction35c(
                                Opcode.INVOKE_STATIC,
                                2,
                                0,
                                1,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        "Ljava/util/Arrays;", "fill", List.of("[I", "I"), "V")),
                        new BuilderInstruction10x(Opcode.RETURN_VOID));

        return new ImmutableMethod(
                AUDIO,
                DEFAULTS,
                List.of(),
                "V",
                AccessFlags.PRIVATE.getValue() | AccessFlags.STATIC.getValue(),
                Set.of(),
                Set.of(),
                new ImmutableMethodImplementation(2, instructions, List.of(), List.of()));
    }

    static Method reset() {
        var b = new MutableMethodImplementation(9);

        b.addInstruction(
                new BuilderInstruction22c(
                        Opcode.IGET_OBJECT,
                        0,
                        8,
                        new ImmutableFieldReference(
                                STREAM, "mIndexMap", "Landroid/util/SparseIntArray;")));
        b.addInstruction(new BuilderInstruction11n(Opcode.CONST_4, 1, 0));
        b.addInstruction(new BuilderInstruction11n(Opcode.CONST_4, 6, 0));
        b.addInstruction(new BuilderInstruction11n(Opcode.CONST_4, 7, -2));
        b.addInstruction(
                new BuilderInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        1,
                        0,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Landroid/util/SparseIntArray;", "size", List.of(), "I")));
        b.addInstruction(new BuilderInstruction11x(Opcode.MOVE_RESULT, 2));
        b.addInstruction(new BuilderInstruction10x(Opcode.NOP));
        b.addInstruction(
                new BuilderInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        2,
                        0,
                        1,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Landroid/util/SparseIntArray;", "keyAt", List.of("I"), "I")));
        b.addInstruction(new BuilderInstruction11x(Opcode.MOVE_RESULT, 3));
        b.addInstruction(
                new BuilderInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        3,
                        0,
                        1,
                        6,
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Landroid/util/SparseIntArray;",
                                "setValueAt",
                                List.of("I", "I"),
                                "V")));
        b.addInstruction(
                new BuilderInstruction22c(
                        Opcode.IGET_OBJECT,
                        4,
                        8,
                        new ImmutableFieldReference(STREAM, "this$0", AUDIO)));
        b.addInstruction(
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        1,
                        4,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                AUDIO,
                                "access$3200",
                                List.of(AUDIO),
                                "Landroid/content/ContentResolver;")));
        b.addInstruction(new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 4));
        b.addInstruction(
                new BuilderInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        2,
                        8,
                        3,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                STREAM,
                                "getSettingNameForDevice",
                                List.of("I"),
                                "Ljava/lang/String;")));
        b.addInstruction(new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 5));
        b.addInstruction(
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        4,
                        4,
                        5,
                        6,
                        7,
                        0,
                        new ImmutableMethodReference(
                                "Landroid/provider/Settings$System;",
                                "putIntForUser",
                                List.of(
                                        "Landroid/content/ContentResolver;",
                                        "Ljava/lang/String;",
                                        "I",
                                        "I"),
                                "Z")));
        b.addInstruction(new BuilderInstruction22b(Opcode.ADD_INT_LIT8, 1, 1, 1));
        b.addInstruction(new BuilderInstruction10x(Opcode.NOP));
        b.addInstruction(new BuilderInstruction31i(Opcode.CONST, 2, 0x40000000));
        b.addInstruction(
                new BuilderInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        3,
                        0,
                        2,
                        6,
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Landroid/util/SparseIntArray;", "put", List.of("I", "I"), "V")));
        b.addInstruction(new BuilderInstruction10x(Opcode.RETURN_VOID));
        b.replaceInstruction(
                6, new BuilderInstruction22t(Opcode.IF_GE, 1, 2, b.newLabelForIndex(18)));
        b.replaceInstruction(17, new BuilderInstruction10t(Opcode.GOTO, b.newLabelForIndex(4)));

        return new ImmutableMethod(
                STREAM,
                RESET,
                List.of(),
                "V",
                AccessFlags.PRIVATE.getValue(),
                Set.of(),
                Set.of(),
                b);
    }

    static Map<String, ClassDef> transform(Map<String, ClassDef> input, boolean inverse)
            throws Exception {
        var out = new TreeMap<String, ClassDef>();
        int changed = 0, helpers = 0;

        for (var c : input.values()) {
            if (!c.getType().equals(AUDIO) && !c.getType().equals(STREAM)) {
                out.put(c.getType(), c);
                continue;
            }

            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();

            for (var m : c.getDirectMethods()) {
                if (m.getName().equals(DEFAULTS) || m.getName().equals(RESET)) {
                    need(
                            inverse
                                    && FrameworkDexMaterializer.semantic(m)
                                            .equals(
                                                    FrameworkDexMaterializer.semantic(
                                                            m.getName().equals(DEFAULTS)
                                                                    ? defaults()
                                                                    : reset())));
                    helpers++;
                    continue;
                }

                if (target(m)) {
                    changed++;
                }

                direct.add(rewrite(m, inverse));
            }

            if (!inverse) {
                direct.add(c.getType().equals(AUDIO) ? defaults() : reset());
            }

            for (var m : c.getVirtualMethods()) {
                if (target(m)) {
                    changed++;
                }

                virtual.add(rewrite(m, inverse));
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

        need(changed == 3 && helpers == (inverse ? 2 : 0));

        return out;
    }

    static byte[] apply(String profile, int api, byte[] original) throws Exception {
        need(api == 28 && original != null);
        var before = PassionServiceForegroundGuard.classes(original);
        var expected = transform(before, false);
        byte[] bytes =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(28), expected.values()));
        var actual = PassionServiceForegroundGuard.classes(bytes);
        need(actual.keySet().equals(before.keySet()));

        for (var key : expected.keySet()) {
            need(
                    FrameworkDexMaterializer.semantic(expected.get(key))
                            .equals(FrameworkDexMaterializer.semantic(actual.get(key))));
        }

        var inverse = transform(actual, true);

        for (var key : before.keySet()) {
            need(
                    FrameworkDexMaterializer.semantic(before.get(key))
                            .equals(FrameworkDexMaterializer.semantic(inverse.get(key))));
        }

        return bytes;
    }
}
