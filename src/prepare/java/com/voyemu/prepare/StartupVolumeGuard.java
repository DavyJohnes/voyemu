package com.voyemu.prepare;

import org.jf.dexlib2.AccessFlags;
import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.BuilderInstruction;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderArrayPayload;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11n;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21s;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22b;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction31i;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction3rc;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.debug.DebugItem;
import org.jf.dexlib2.iface.debug.EndLocal;
import org.jf.dexlib2.iface.debug.EpilogueBegin;
import org.jf.dexlib2.iface.debug.LineNumber;
import org.jf.dexlib2.iface.debug.PrologueEnd;
import org.jf.dexlib2.iface.debug.RestartLocal;
import org.jf.dexlib2.iface.debug.SetSourceFile;
import org.jf.dexlib2.iface.debug.StartLocal;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.formats.ArrayPayload;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.ImmutableMethodParameter;
import org.jf.dexlib2.immutable.debug.ImmutableDebugItem;
import org.jf.dexlib2.immutable.debug.ImmutableEndLocal;
import org.jf.dexlib2.immutable.debug.ImmutableEpilogueBegin;
import org.jf.dexlib2.immutable.debug.ImmutableLineNumber;
import org.jf.dexlib2.immutable.debug.ImmutablePrologueEnd;
import org.jf.dexlib2.immutable.debug.ImmutableRestartLocal;
import org.jf.dexlib2.immutable.debug.ImmutableSetSourceFile;
import org.jf.dexlib2.immutable.debug.ImmutableStartLocal;
import org.jf.dexlib2.immutable.reference.ImmutableFieldReference;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/** Start each platform stream/group at zero; subsequent volume controls remain stock. */
final class StartupVolumeGuard {
    static final String NAME = "api30-startup-volume";

    static final String AUDIO = "Lcom/android/server/audio/AudioService;",
            STREAM = AUDIO.substring(0, AUDIO.length() - 1) + "$VolumeStreamState;",
            GROUP = AUDIO.substring(0, AUDIO.length() - 1) + "$VolumeGroupState;";
    static final List<Integer> MIN = List.of(1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0);
    static final Set<String> OWNERS = Set.of(AUDIO, STREAM, GROUP);

    static void need(boolean b) {
        Failure.require(
                b, "STARTUP_VOLUME_CONTRACT", "Exact cold-start volume initialization required", 3);
    }

    static final String PERSIST = "voyemuPersistStartupVolume";

    static boolean target(Method m) {
        return (m.getDefiningClass().equals(AUDIO)
                        && (m.getName().equals("<clinit>")
                                || (m.getName().equals("<init>")
                                        && m.getParameterTypes().size() == 3)))
                || (m.getDefiningClass().equals(STREAM)
                        && m.getName().equals("<init>")
                        && m.getParameterTypes().size() == 3)
                || (m.getDefiningClass().equals(GROUP)
                        && m.getName().equals("<init>")
                        && m.getParameterTypes().size() == 2);
    }

    static Method copy(Method m, MutableMethodImplementation body) {
        // Same retained-field relocation as NetworkGuard: builder locals omit inferred
        // names/types.
        body.getInstructions();
        var original = new ArrayList<DebugItem>();
        m.getImplementation().getDebugItems().forEach(original::add);
        var moved = new ArrayList<DebugItem>();
        body.getDebugItems().forEach(moved::add);
        need(original.size() == moved.size());
        var debug = new ArrayList<ImmutableDebugItem>();

        for (int i = 0; i < original.size(); i++) {
            var item = original.get(i);
            int at = moved.get(i).getCodeAddress();
            need(item.getDebugItemType() == moved.get(i).getDebugItemType());

            switch (item) {
                case StartLocal v ->
                        debug.add(
                                new ImmutableStartLocal(
                                        at,
                                        v.getRegister(),
                                        v.getName(),
                                        v.getType(),
                                        v.getSignature()));
                case EndLocal v ->
                        debug.add(
                                new ImmutableEndLocal(
                                        at,
                                        v.getRegister(),
                                        v.getName(),
                                        v.getType(),
                                        v.getSignature()));
                case RestartLocal v ->
                        debug.add(
                                new ImmutableRestartLocal(
                                        at,
                                        v.getRegister(),
                                        v.getName(),
                                        v.getType(),
                                        v.getSignature()));
                case LineNumber v -> debug.add(new ImmutableLineNumber(at, v.getLineNumber()));
                case PrologueEnd _ -> debug.add(new ImmutablePrologueEnd(at));
                case EpilogueBegin _ -> debug.add(new ImmutableEpilogueBegin(at));
                case SetSourceFile v ->
                        debug.add(new ImmutableSetSourceFile(at, v.getSourceFile()));
                default -> need(false);
            }
        }

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(
                        body.getRegisterCount(),
                        body.getInstructions(),
                        body.getTryBlocks(),
                        debug));
    }

    static Method rewrite(Method m, boolean inverse) throws Exception {
        if (!target(m)) {
            return m;
        }

        need(m.getImplementation() != null);
        var body = new MutableMethodImplementation(m.getImplementation());
        int found;

        if (m.getDefiningClass().equals(AUDIO) && m.getName().equals("<clinit>")) {
            found = rewriteVolumeTable(body, inverse);
        } else if (m.getDefiningClass().equals(AUDIO)) {
            found = rewriteThreadDefaults(body, inverse);
        } else {
            found = rewriteSettingsReset(m, body, inverse);
        }

        need(found == 1);

        return copy(m, body);
    }

    static int rewriteVolumeTable(MutableMethodImplementation body, boolean inverse) {
        int found = 0;

        for (int i = 0; i < body.getInstructions().size(); i++) {
            if (body.getInstructions().get(i) instanceof ArrayPayload a) {
                var values = a.getArrayElements().stream().map(Number::intValue).toList();

                if (values.equals(inverse ? Collections.nCopies(12, 0) : MIN)) {
                    need(a.getElementWidth() == 4);
                    body.replaceInstruction(
                            i,
                            new BuilderArrayPayload(
                                    4,
                                    new ArrayList<Number>(
                                            inverse ? MIN : Collections.nCopies(12, 0))));
                    found++;
                }
            }
        }

        return found;
    }

    static int rewriteThreadDefaults(MutableMethodImplementation body, boolean inverse) {
        int address = 0;

        for (int i = 0; i < body.getInstructions().size(); i++) {
            var ins = body.getInstructions().get(i);

            if (ins instanceof ReferenceInstruction r
                    && r.getReference().toString().equals(AUDIO + "->createAudioSystemThread()V")) {
                need(address == (inverse ? 747 : 740) && body.getRegisterCount() == 33);

                if (inverse) {
                    for (int n = 0; n < 3; n++) {
                        body.removeInstruction(i - 3);
                    }
                } else {
                    body.addInstruction(
                            i,
                            new BuilderInstruction21c(
                                    Opcode.SGET_OBJECT,
                                    31,
                                    new ImmutableFieldReference(
                                            "Landroid/media/AudioSystem;",
                                            "DEFAULT_STREAM_VOLUME",
                                            "[I")));
                    body.addInstruction(i + 1, new BuilderInstruction21s(Opcode.CONST_16, 32, 0));
                    body.addInstruction(
                            i + 2,
                            new BuilderInstruction3rc(
                                    Opcode.INVOKE_STATIC_RANGE,
                                    31,
                                    2,
                                    new ImmutableMethodReference(
                                            "Ljava/util/Arrays;",
                                            "fill",
                                            List.of("[I", "I"),
                                            "V")));
                }

                return 1;
            }

            address += ins.getCodeUnits();
        }

        return 0;
    }

    static int rewriteSettingsReset(Method m, MutableMethodImplementation body, boolean inverse) {
        boolean stream = m.getDefiningClass().equals(STREAM);
        int self = stream ? 2 : 5,
                map = stream ? 3 : 6,
                index = 4,
                key = stream ? 5 : 7,
                address = 0;

        need(body.getRegisterCount() == (stream ? 6 : 8));

        for (int i = 0; i < body.getInstructions().size(); i++) {
            var ins = body.getInstructions().get(i);
            String ref = ins instanceof ReferenceInstruction r ? r.getReference().toString() : "";

            if (ref.equals(m.getDefiningClass() + "->readSettings()V")) {
                need(
                        address == (stream ? 45 : 134)
                                && ins.getOpcode() == Opcode.INVOKE_VIRTUAL
                                && ((FiveRegisterInstruction) ins).getRegisterC() == self);

                if (inverse) {
                    for (int n = 0; n < 15; n++) {
                        body.removeInstruction(i + 1);
                    }
                } else {
                    List<BuilderInstruction> block =
                            resetBlock(m.getDefiningClass(), stream, self, map, index, key);

                    for (int n = 0; n < block.size(); n++) {
                        body.addInstruction(i + 1 + n, block.get(n));
                    }

                    body.replaceInstruction(
                            i + 5,
                            new BuilderInstruction22t(
                                    Opcode.IF_GE, index, key, body.newLabelForIndex(i + 13)));
                    body.replaceInstruction(
                            i + 12,
                            new BuilderInstruction10t(Opcode.GOTO, body.newLabelForIndex(i + 3)));
                }

                return 1;
            }

            address += ins.getCodeUnits();
        }

        return 0;
    }

    // Inserted after readSettings(): zeroes and persists every indexed volume, then marks the map.
    static List<BuilderInstruction> resetBlock(
            String owner, boolean stream, int self, int map, int index, int key) {
        var field =
                new ImmutableFieldReference(owner, "mIndexMap", "Landroid/util/SparseIntArray;");

        return new ArrayList<>(
                List.of(
                        new BuilderInstruction22c(Opcode.IGET_OBJECT, map, self, field),
                        new BuilderInstruction11n(Opcode.CONST_4, index, 0),
                        new BuilderInstruction35c(
                                Opcode.INVOKE_VIRTUAL,
                                1,
                                map,
                                0,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        "Landroid/util/SparseIntArray;", "size", List.of(), "I")),
                        new BuilderInstruction11x(Opcode.MOVE_RESULT, key),
                        new BuilderInstruction10x(Opcode.NOP),
                        new BuilderInstruction35c(
                                Opcode.INVOKE_VIRTUAL,
                                2,
                                map,
                                index,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        "Landroid/util/SparseIntArray;",
                                        "keyAt",
                                        List.of("I"),
                                        "I")),
                        new BuilderInstruction11x(Opcode.MOVE_RESULT, key),
                        new BuilderInstruction11n(Opcode.CONST_4, 0, 0),
                        new BuilderInstruction35c(
                                Opcode.INVOKE_VIRTUAL,
                                3,
                                map,
                                index,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        "Landroid/util/SparseIntArray;",
                                        "setValueAt",
                                        List.of("I", "I"),
                                        "V")),
                        new BuilderInstruction35c(
                                Opcode.INVOKE_DIRECT,
                                2,
                                self,
                                key,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        owner,
                                        stream ? PERSIST : "persistVolumeGroup",
                                        List.of("I"),
                                        "V")),
                        new BuilderInstruction22b(Opcode.ADD_INT_LIT8, index, index, 1),
                        new BuilderInstruction10x(Opcode.NOP),
                        new BuilderInstruction11n(Opcode.CONST_4, 0, 0),
                        new BuilderInstruction31i(Opcode.CONST, 1, 0x40000000),
                        new BuilderInstruction35c(
                                Opcode.INVOKE_VIRTUAL,
                                3,
                                map,
                                1,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        "Landroid/util/SparseIntArray;",
                                        "put",
                                        List.of("I", "I"),
                                        "V"))));
    }

    static Method persistHelper() {
        var code =
                List.<org.jf.dexlib2.iface.instruction.Instruction>of(
                        new BuilderInstruction22c(
                                Opcode.IGET_OBJECT,
                                0,
                                4,
                                new ImmutableFieldReference(STREAM, "this$0", AUDIO)),
                        new BuilderInstruction35c(
                                Opcode.INVOKE_STATIC,
                                1,
                                0,
                                0,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        AUDIO,
                                        "access$2700",
                                        List.of(AUDIO),
                                        "Landroid/content/ContentResolver;")),
                        new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0),
                        new BuilderInstruction35c(
                                Opcode.INVOKE_VIRTUAL,
                                2,
                                4,
                                5,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        STREAM,
                                        "getSettingNameForDevice",
                                        List.of("I"),
                                        "Ljava/lang/String;")),
                        new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 1),
                        new BuilderInstruction11n(Opcode.CONST_4, 2, 0),
                        new BuilderInstruction11n(Opcode.CONST_4, 3, -2),
                        new BuilderInstruction35c(
                                Opcode.INVOKE_STATIC,
                                4,
                                0,
                                1,
                                2,
                                3,
                                0,
                                new ImmutableMethodReference(
                                        "Landroid/provider/Settings$System;",
                                        "putIntForUser",
                                        List.of(
                                                "Landroid/content/ContentResolver;",
                                                "Ljava/lang/String;",
                                                "I",
                                                "I"),
                                        "Z")),
                        new BuilderInstruction10x(Opcode.RETURN_VOID));

        return new ImmutableMethod(
                STREAM,
                PERSIST,
                List.of(new ImmutableMethodParameter("I", Set.of(), null)),
                "V",
                AccessFlags.PRIVATE.getValue(),
                Set.of(),
                Set.of(),
                new ImmutableMethodImplementation(6, code, List.of(), List.of()));
    }

    static Map<String, ClassDef> transform(Map<String, ClassDef> input, boolean inverse)
            throws Exception {
        var out = new TreeMap<String, ClassDef>();
        int changed = 0, helpers = 0;

        for (var c : input.values()) {
            if (!OWNERS.contains(c.getType())) {
                out.put(c.getType(), c);
                continue;
            }

            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();

            for (var m : c.getDirectMethods()) {
                if (m.getName().equals(PERSIST)) {
                    need(
                            inverse
                                    && FrameworkDexMaterializer.semantic(m)
                                            .equals(
                                                    FrameworkDexMaterializer.semantic(
                                                            persistHelper())));
                    helpers++;
                    continue;
                }

                if (target(m)) {
                    changed++;
                }

                direct.add(rewrite(m, inverse));
            }

            if (c.getType().equals(STREAM) && !inverse) {
                direct.add(persistHelper());
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

        need(changed == 4 && helpers == (inverse ? 1 : 0));

        return out;
    }

    /**
     * The step constant startup_volume_shared selects the clean rewrite shared with Passion,
     * otherwise the pinned transform of this class with its own serialization checks.
     */
    static void validate(Map<String, Object> constants, int api, Path original, byte[] dex)
            throws Exception {
        if (!StepConstants.flag(constants, "startup_volume_shared")) {
            validateOriginal(constants, api, original, dex);

            return;
        }

        need(
                api == 30
                        && dex != null
                        && StepConstants.admits(constants, FilesSupport.hash(original)));
        rewriter().validate(PassionServiceForegroundGuard.classes(dex));
    }

    static void validateOriginal(Map<String, Object> constants, int api, Path original, byte[] dex)
            throws Exception {
        need(api == 30 && StepConstants.admits(constants, FilesSupport.hash(original)));
        transform(PassionServiceForegroundGuard.classes(dex), false);
    }

    // Callers verify their own DEX pins first, so every services.jar whose step selects the shared
    // rewrite reuses the identical pinned instruction sequence (streams to minimum, groups to
    // zero).
    static Rewriter rewriter() {
        return new Rewriter();
    }

    static final class Rewriter {
        void need(boolean accepted) {
            Failure.require(
                    accepted,
                    "STARTUP_VOLUME_CONTRACT",
                    "Exact cold-start volume initialization required",
                    3);
        }

        static void requireContract(boolean accepted, String code, String message) {
            Failure.require(accepted, code, message, 3);
        }

        private Method rewrite(Method m, boolean inverse) throws Exception {
            if (!target(m)) {
                return m;
            }

            need(m.getImplementation() != null);
            var body = new MutableMethodImplementation(m.getImplementation());
            int found;

            if (m.getDefiningClass().equals(AUDIO) && m.getName().equals("<clinit>")) {
                found = rewriteVolumeTable(body, inverse);
            } else if (m.getDefiningClass().equals(AUDIO)) {
                found = rewriteThreadDefaults(body, inverse);
            } else {
                found = rewriteSettingsReset(m, body, inverse);
            }

            need(found == 1);

            return copy(m, body);
        }

        private int rewriteVolumeTable(MutableMethodImplementation body, boolean inverse) {
            int found = 0;

            for (int i = 0; i < body.getInstructions().size(); i++) {
                if (body.getInstructions().get(i) instanceof ArrayPayload a) {
                    var values = a.getArrayElements().stream().map(Number::intValue).toList();

                    if (values.equals(inverse ? Collections.nCopies(12, 0) : MIN)) {
                        need(a.getElementWidth() == 4);
                        body.replaceInstruction(
                                i,
                                new BuilderArrayPayload(
                                        4,
                                        new ArrayList<Number>(
                                                inverse ? MIN : Collections.nCopies(12, 0))));
                        found++;
                    }
                }
            }

            return found;
        }

        private int rewriteThreadDefaults(MutableMethodImplementation body, boolean inverse) {
            int address = 0;

            for (int i = 0; i < body.getInstructions().size(); i++) {
                var ins = body.getInstructions().get(i);

                if (ins instanceof ReferenceInstruction r
                        && r.getReference()
                                .toString()
                                .equals(AUDIO + "->createAudioSystemThread()V")) {
                    need(address == (inverse ? 747 : 740) && body.getRegisterCount() == 33);

                    if (inverse) {
                        for (int n = 0; n < 3; n++) {
                            body.removeInstruction(i - 3);
                        }
                    } else {
                        body.addInstruction(
                                i,
                                new BuilderInstruction21c(
                                        Opcode.SGET_OBJECT,
                                        31,
                                        new ImmutableFieldReference(
                                                "Landroid/media/AudioSystem;",
                                                "DEFAULT_STREAM_VOLUME",
                                                "[I")));
                        body.addInstruction(
                                i + 1, new BuilderInstruction21s(Opcode.CONST_16, 32, 0));
                        body.addInstruction(
                                i + 2,
                                new BuilderInstruction3rc(
                                        Opcode.INVOKE_STATIC_RANGE,
                                        31,
                                        2,
                                        new ImmutableMethodReference(
                                                "Ljava/util/Arrays;",
                                                "fill",
                                                List.of("[I", "I"),
                                                "V")));
                    }

                    return 1;
                }

                address += ins.getCodeUnits();
            }

            return 0;
        }

        private int rewriteSettingsReset(
                Method m, MutableMethodImplementation body, boolean inverse) {
            boolean stream = m.getDefiningClass().equals(STREAM);
            int self = stream ? 2 : 5,
                    map = stream ? 3 : 6,
                    index = 4,
                    key = stream ? 5 : 7,
                    address = 0;

            need(body.getRegisterCount() == (stream ? 6 : 8));

            for (int i = 0; i < body.getInstructions().size(); i++) {
                var ins = body.getInstructions().get(i);
                String ref =
                        ins instanceof ReferenceInstruction r ? r.getReference().toString() : "";

                if (ref.equals(m.getDefiningClass() + "->readSettings()V")) {
                    need(
                            address == (stream ? 45 : 134)
                                    && ins.getOpcode() == Opcode.INVOKE_VIRTUAL
                                    && ((FiveRegisterInstruction) ins).getRegisterC() == self);

                    if (inverse) {
                        for (int n = 0; n < 15; n++) {
                            body.removeInstruction(i + 1);
                        }
                    } else {
                        List<BuilderInstruction> block =
                                resetBlock(m.getDefiningClass(), stream, self, map, index, key);

                        for (int n = 0; n < block.size(); n++) {
                            body.addInstruction(i + 1 + n, block.get(n));
                        }

                        body.replaceInstruction(
                                i + 5,
                                new BuilderInstruction22t(
                                        Opcode.IF_GE, index, key, body.newLabelForIndex(i + 13)));
                        body.replaceInstruction(
                                i + 12,
                                new BuilderInstruction10t(
                                        Opcode.GOTO, body.newLabelForIndex(i + 3)));
                    }

                    return 1;
                }

                address += ins.getCodeUnits();
            }

            return 0;
        }

        private Method persistHelper() {
            var code =
                    List.<org.jf.dexlib2.iface.instruction.Instruction>of(
                            new BuilderInstruction22c(
                                    Opcode.IGET_OBJECT,
                                    0,
                                    4,
                                    new ImmutableFieldReference(STREAM, "this$0", AUDIO)),
                            new BuilderInstruction35c(
                                    Opcode.INVOKE_STATIC,
                                    1,
                                    0,
                                    0,
                                    0,
                                    0,
                                    0,
                                    new ImmutableMethodReference(
                                            AUDIO,
                                            "access$2700",
                                            List.of(AUDIO),
                                            "Landroid/content/ContentResolver;")),
                            new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0),
                            new BuilderInstruction35c(
                                    Opcode.INVOKE_VIRTUAL,
                                    2,
                                    4,
                                    5,
                                    0,
                                    0,
                                    0,
                                    new ImmutableMethodReference(
                                            STREAM,
                                            "getSettingNameForDevice",
                                            List.of("I"),
                                            "Ljava/lang/String;")),
                            new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 1),
                            new BuilderInstruction11n(Opcode.CONST_4, 2, 0),
                            new BuilderInstruction11n(Opcode.CONST_4, 3, -2),
                            new BuilderInstruction35c(
                                    Opcode.INVOKE_STATIC,
                                    4,
                                    0,
                                    1,
                                    2,
                                    3,
                                    0,
                                    new ImmutableMethodReference(
                                            "Landroid/provider/Settings$System;",
                                            "putIntForUser",
                                            List.of(
                                                    "Landroid/content/ContentResolver;",
                                                    "Ljava/lang/String;",
                                                    "I",
                                                    "I"),
                                            "Z")),
                            new BuilderInstruction10x(Opcode.RETURN_VOID));

            return new ImmutableMethod(
                    STREAM,
                    PERSIST,
                    List.of(new ImmutableMethodParameter("I", Set.of(), null)),
                    "V",
                    AccessFlags.PRIVATE.getValue(),
                    Set.of(),
                    Set.of(),
                    new ImmutableMethodImplementation(6, code, List.of(), List.of()));
        }

        Map<String, ClassDef> transform(Map<String, ClassDef> input, boolean inverse)
                throws Exception {
            var out = new TreeMap<String, ClassDef>();
            int changed = 0, helpers = 0;

            for (var c : input.values()) {
                if (!OWNERS.contains(c.getType())) {
                    out.put(c.getType(), c);
                    continue;
                }

                var direct = new ArrayList<Method>();
                var virtual = new ArrayList<Method>();

                for (var m : c.getDirectMethods()) {
                    if (m.getName().equals(PERSIST)) {
                        need(
                                inverse
                                        && FrameworkDexMaterializer.semantic(m)
                                                .equals(
                                                        FrameworkDexMaterializer.semantic(
                                                                persistHelper())));
                        helpers++;
                        continue;
                    }

                    if (target(m)) {
                        changed++;
                    }

                    direct.add(rewrite(m, inverse));
                }

                if (c.getType().equals(STREAM) && !inverse) {
                    direct.add(persistHelper());
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

            need(changed == 4 && helpers == (inverse ? 1 : 0));

            return out;
        }

        void validate(Map<String, ClassDef> before) throws Exception {
            transform(before, false);
        }

        byte[] applyBytes(Map<String, ClassDef> before) throws Exception {
            var expected = transform(before, false);
            byte[] output =
                    FrameworkDexMaterializer.serialize(
                            new ImmutableDexFile(Opcodes.forApi(30), expected.values()));
            var actual = PassionServiceForegroundGuard.classes(output);

            need(actual.keySet().equals(before.keySet()));

            for (var key : expected.keySet()) {
                need(
                        FrameworkDexMaterializer.semantic(expected.get(key))
                                .equals(FrameworkDexMaterializer.semantic(actual.get(key))));
            }

            var restored = transform(actual, true);

            for (var key : before.keySet()) {
                need(
                        FrameworkDexMaterializer.semantic(before.get(key))
                                .equals(FrameworkDexMaterializer.semantic(restored.get(key))));
            }

            return output;
        }
    }

    static byte[] apply(Map<String, Object> constants, int api, Path original, byte[] predecessor)
            throws Exception {
        if (StepConstants.flag(constants, "startup_volume_shared")) {
            validate(constants, api, original, predecessor);

            return rewriter().applyBytes(PassionServiceForegroundGuard.classes(predecessor));
        }

        need(api == 30 && StepConstants.admits(constants, FilesSupport.hash(original)));
        var before = PassionServiceForegroundGuard.classes(predecessor);
        var expected = transform(before, false);
        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), expected.values()));
        var actual = PassionServiceForegroundGuard.classes(output);
        need(actual.keySet().equals(before.keySet()));

        for (var key : expected.keySet()) {
            if (!FrameworkDexMaterializer.semantic(expected.get(key))
                    .equals(FrameworkDexMaterializer.semantic(actual.get(key)))) {
                throw new Failure(
                        "STARTUP_VOLUME_CONTRACT",
                        "Serialized class differs: "
                                + key
                                + " "
                                + FrameworkDexMaterializer.difference(
                                        expected.get(key), actual.get(key)),
                        3);
            }
        }

        var restored = transform(actual, true);

        for (var key : before.keySet()) {
            Failure.require(
                    FrameworkDexMaterializer.semantic(before.get(key))
                            .equals(FrameworkDexMaterializer.semantic(restored.get(key))),
                    "STARTUP_VOLUME_CONTRACT",
                    "Inverse class differs: " + key,
                    3);
        }

        return output;
    }
}
