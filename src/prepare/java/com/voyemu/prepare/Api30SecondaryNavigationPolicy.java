package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.Label;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11n;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction12x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction3rc;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.debug.DebugItem;
import org.jf.dexlib2.iface.debug.EndLocal;
import org.jf.dexlib2.iface.debug.EpilogueBegin;
import org.jf.dexlib2.iface.debug.LineNumber;
import org.jf.dexlib2.iface.debug.PrologueEnd;
import org.jf.dexlib2.iface.debug.RestartLocal;
import org.jf.dexlib2.iface.debug.SetSourceFile;
import org.jf.dexlib2.iface.debug.StartLocal;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.debug.ImmutableDebugItem;
import org.jf.dexlib2.immutable.debug.ImmutableEndLocal;
import org.jf.dexlib2.immutable.debug.ImmutableEpilogueBegin;
import org.jf.dexlib2.immutable.debug.ImmutableLineNumber;
import org.jf.dexlib2.immutable.debug.ImmutablePrologueEnd;
import org.jf.dexlib2.immutable.debug.ImmutableRestartLocal;
import org.jf.dexlib2.immutable.debug.ImmutableSetSourceFile;
import org.jf.dexlib2.immutable.debug.ImmutableStartLocal;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction11n;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction11x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction12x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21t;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction22c;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction22t;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction35c;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction3rc;
import org.jf.dexlib2.immutable.reference.ImmutableFieldReference;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;

/**
 * Exact clean API30 secondary stock navigation policy; OEM display decorations remain supported.
 */
final class Api30SecondaryNavigationPolicy {
    static final String PIN = "c6992cdc2a3e23d5aeb6ab77e7a7f6889cfcf5c262c6ed86825e6fcbf601681d";
    static final String TYPE = "Lcom/android/server/wm/DisplayPolicy;";

    private static void require(boolean value) {
        Failure.require(
                value,
                "API30_SECONDARY_NAVIGATION_CONTRACT",
                "Exact registered clean API30 secondary navigation policy required",
                3);
    }

    private static Method rewrite(Method method, boolean inverse, int[] changed) {
        if (!method.getName().equals("<init>")) {
            return method;
        }

        require(
                method.getParameterTypes()
                                .equals(
                                        List.of(
                                                "Lcom/android/server/wm/WindowManagerService;",
                                                "Lcom/android/server/wm/DisplayContent;"))
                        && method.getReturnType().equals("V")
                        && method.getImplementation() != null);
        var body = method.getImplementation();
        require(body.getRegisterCount() == 20);
        var out = new ArrayList<Instruction>();
        int address = 0, invokes = 0, assigns = 0, mainkeys = 0;

        for (Instruction instruction : body.getInstructions()) {
            String reference =
                    instruction instanceof ReferenceInstruction r
                            ? r.getReference().toString()
                            : "";

            if (reference.equals(
                    "Lcom/android/server/wm/DisplayContent;->supportsSystemDecorations()Z")) {
                require(address == 0x191 && instruction.getOpcode() == Opcode.INVOKE_VIRTUAL);
                invokes++;
            }

            if (reference.equals(TYPE + "->mHasNavigationBar:Z")
                    && instruction.getOpcode() == Opcode.IPUT_BOOLEAN) {
                assigns++;

                if (address == 0x195) {
                    require(
                            ((org.jf.dexlib2.iface.instruction.TwoRegisterInstruction) instruction)
                                                    .getRegisterA()
                                            == 3
                                    && ((org.jf.dexlib2.iface.instruction.TwoRegisterInstruction)
                                                            instruction)
                                                    .getRegisterB()
                                            == 0);
                }
            }

            if (reference.equals("qemu.hw.mainkeys")) {
                mainkeys++;
            }

            if (address == 0x194) {
                out.add(navigationValue(instruction, inverse));
                changed[0]++;
            } else {
                out.add(instruction);
            }

            address += instruction.getCodeUnits();
        }

        require(invokes == 1 && assigns == 4 && mainkeys == 1);

        return new ImmutableMethod(
                method.getDefiningClass(),
                method.getName(),
                method.getParameters(),
                method.getReturnType(),
                method.getAccessFlags(),
                method.getAnnotations(),
                method.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(
                        body.getRegisterCount(), out, body.getTryBlocks(), body.getDebugItems()));
    }

    private static Instruction navigationValue(Instruction instruction, boolean inverse) {
        require(
                instruction instanceof OneRegisterInstruction oneRegisterInstruction
                        && oneRegisterInstruction.getRegisterA() == 3
                        && instruction.getOpcode()
                                == (inverse ? Opcode.CONST_4 : Opcode.MOVE_RESULT));

        if (inverse) {
            require(
                    ((org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction) instruction)
                                    .getNarrowLiteral()
                            == 0);
        }

        return inverse
                ? new ImmutableInstruction11x(Opcode.MOVE_RESULT, 3)
                : new ImmutableInstruction11n(Opcode.CONST_4, 3, 0);
    }

    private static ImmutableDexFile rewrite(DexFile source, boolean inverse) {
        var result = new ArrayList<ClassDef>();
        int[] changed = {0};
        int found = 0;

        for (ClassDef type : source.getClasses()) {
            if (!type.getType().equals(TYPE)) {
                result.add(type);
                continue;
            }

            found++;
            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();

            for (Method method : type.getDirectMethods()) {
                direct.add(rewrite(method, inverse, changed));
            }

            for (Method method : type.getVirtualMethods()) {
                virtual.add(method);
            }

            result.add(
                    new ImmutableClassDef(
                            type.getType(),
                            type.getAccessFlags(),
                            type.getSuperclass(),
                            type.getInterfaces(),
                            type.getSourceFile(),
                            type.getAnnotations(),
                            type.getStaticFields(),
                            type.getInstanceFields(),
                            direct,
                            virtual));
        }

        require(found == 1 && changed[0] == 1);

        return new ImmutableDexFile(Opcodes.forApi(30), result);
    }

    static void validate(int api, Path original, byte[] dex) throws Exception {
        require(
                api == 30
                        && original != null
                        && FilesSupport.hash(original).equals(PIN)
                        && dex != null);
        rewrite(new DexBackedDexFile(Opcodes.forApi(30), dex), false);
    }

    static byte[] apply(int api, Path original, byte[] dex) throws Exception {
        validate(api, original, dex);
        var before = new DexBackedDexFile(Opcodes.forApi(30), dex);
        byte[] bytes = FrameworkDexMaterializer.serialize(rewrite(before, false));
        var restored = rewrite(new DexBackedDexFile(Opcodes.forApi(30), bytes), true);
        var names = new TreeMap<String, ClassDef>();

        for (ClassDef type : restored.getClasses()) {
            names.put(type.getType(), type);
        }

        require(names.size() == before.getClasses().size());

        for (ClassDef type : before.getClasses()) {
            require(
                    names.containsKey(type.getType())
                            && FrameworkDexMaterializer.semantic(type)
                                    .equals(
                                            FrameworkDexMaterializer.semantic(
                                                    names.get(type.getType()))));
        }

        return bytes;
    }

    // Explicit Passion mode; the existing three-argument Free rewrite remains byte-identical.
    private static final String DECOR =
            "Lcom/android/server/wm/DisplayContent;->supportsSystemDecorations()Z";

    private static List<Instruction> passionBlock() {
        return List.of(
                new ImmutableInstruction11x(Opcode.MOVE_RESULT, 3),
                new ImmutableInstruction21t(Opcode.IF_EQZ, 3, 10),
                new ImmutableInstruction3rc(
                        Opcode.INVOKE_VIRTUAL_RANGE,
                        19,
                        1,
                        new ImmutableMethodReference(
                                "Lcom/android/server/wm/DisplayContent;",
                                "getDisplayId",
                                List.of(),
                                "I")),
                new ImmutableInstruction11x(Opcode.MOVE_RESULT, 8),
                new ImmutableInstruction11n(Opcode.CONST_4, 9, 1),
                new ImmutableInstruction22t(Opcode.IF_EQ, 8, 9, 3),
                new ImmutableInstruction11n(Opcode.CONST_4, 3, 0),
                new ImmutableInstruction22c(
                        Opcode.IPUT_BOOLEAN,
                        3,
                        0,
                        new ImmutableFieldReference(TYPE, "mHasStatusBar", "Z")),
                new ImmutableInstruction11n(Opcode.CONST_4, 3, 0));
    }

    private static Method passionCopy(Method method, MutableMethodImplementation body) {
        // Retain original local metadata at builder-relocated addresses, as in the existing API30
        // guards.
        body.getInstructions();
        var original = new ArrayList<DebugItem>();
        method.getImplementation().getDebugItems().forEach(original::add);
        var moved = new ArrayList<DebugItem>();
        body.getDebugItems().forEach(moved::add);
        require(original.size() == moved.size());
        var debug = new ArrayList<ImmutableDebugItem>();

        for (int index = 0; index < original.size(); index++) {
            DebugItem item = original.get(index);
            int at = moved.get(index).getCodeAddress();
            require(item.getDebugItemType() == moved.get(index).getDebugItemType());

            switch (item) {
                case StartLocal local ->
                        debug.add(
                                new ImmutableStartLocal(
                                        at,
                                        local.getRegister(),
                                        local.getName(),
                                        local.getType(),
                                        local.getSignature()));
                case EndLocal local ->
                        debug.add(
                                new ImmutableEndLocal(
                                        at,
                                        local.getRegister(),
                                        local.getName(),
                                        local.getType(),
                                        local.getSignature()));
                case RestartLocal local ->
                        debug.add(
                                new ImmutableRestartLocal(
                                        at,
                                        local.getRegister(),
                                        local.getName(),
                                        local.getType(),
                                        local.getSignature()));
                case LineNumber line ->
                        debug.add(new ImmutableLineNumber(at, line.getLineNumber()));
                case PrologueEnd _ -> debug.add(new ImmutablePrologueEnd(at));
                case EpilogueBegin _ -> debug.add(new ImmutableEpilogueBegin(at));
                case SetSourceFile source ->
                        debug.add(new ImmutableSetSourceFile(at, source.getSourceFile()));
                default -> require(false);
            }
        }

        return new ImmutableMethod(
                method.getDefiningClass(),
                method.getName(),
                method.getParameters(),
                method.getReturnType(),
                method.getAccessFlags(),
                method.getAnnotations(),
                method.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(
                        body.getRegisterCount(),
                        body.getInstructions(),
                        body.getTryBlocks(),
                        debug));
    }

    private static Method passionMethod(Method m, boolean inverse, int[] changed) throws Exception {
        if (!m.getName().equals("<init>")) {
            return m;
        }

        require(
                m.getParameterTypes()
                                .equals(
                                        List.of(
                                                "Lcom/android/server/wm/WindowManagerService;",
                                                "Lcom/android/server/wm/DisplayContent;"))
                        && m.getReturnType().equals("V")
                        && m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == 20
                        && m.getImplementation().getTryBlocks().isEmpty());
        var b = new MutableMethodImplementation(m.getImplementation());
        int address = 0, index = -1;

        for (int i = 0; i < b.getInstructions().size(); i++) {
            if (address == 0x194) {
                index = i;
                break;
            }

            address += b.getInstructions().get(i).getCodeUnits();
        }

        require(
                index > 0
                        && b.getInstructions().get(index - 1) instanceof ReferenceInstruction
                        && ((ReferenceInstruction) b.getInstructions().get(index - 1))
                                .getReference()
                                .toString()
                                .equals(DECOR));

        if (inverse) {
            var expected = passionBlock();
            require(index + expected.size() < b.getInstructions().size());

            for (int i = 0; i < expected.size(); i++) {
                require(
                        FrameworkDexMaterializer.semantic(expected.get(i))
                                .equals(
                                        FrameworkDexMaterializer.semantic(
                                                b.getInstructions().get(index + i))));
            }

            for (int i = 0; i < 8; i++) {
                b.removeInstruction(index + 1);
            }

            b.replaceInstruction(index, new BuilderInstruction11n(Opcode.CONST_4, 3, 0));
        } else {
            var old = b.getInstructions().get(index);
            require(
                    old.getOpcode() == Opcode.CONST_4
                            && ((OneRegisterInstruction) old).getRegisterA() == 3
                            && ((org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction) old)
                                            .getNarrowLiteral()
                                    == 0);
            b.replaceInstruction(index, new BuilderInstruction11x(Opcode.MOVE_RESULT, 3));
            b.addInstruction(index + 1, new BuilderInstruction10x(Opcode.NOP));
            b.addInstruction(
                    index + 2,
                    new BuilderInstruction3rc(
                            Opcode.INVOKE_VIRTUAL_RANGE,
                            19,
                            1,
                            new ImmutableMethodReference(
                                    "Lcom/android/server/wm/DisplayContent;",
                                    "getDisplayId",
                                    List.of(),
                                    "I")));
            b.addInstruction(index + 3, new BuilderInstruction11x(Opcode.MOVE_RESULT, 8));
            b.addInstruction(index + 4, new BuilderInstruction11n(Opcode.CONST_4, 9, 1));
            b.addInstruction(index + 5, new BuilderInstruction10x(Opcode.NOP));
            b.addInstruction(index + 6, new BuilderInstruction11n(Opcode.CONST_4, 3, 0));
            b.addInstruction(
                    index + 7,
                    new BuilderInstruction22c(
                            Opcode.IPUT_BOOLEAN,
                            3,
                            0,
                            new ImmutableFieldReference(TYPE, "mHasStatusBar", "Z")));
            b.addInstruction(index + 8, new BuilderInstruction11n(Opcode.CONST_4, 3, 0));
            Label status = b.newLabelForIndex(index + 7);
            b.replaceInstruction(index + 1, new BuilderInstruction21t(Opcode.IF_EQZ, 3, status));
            b.replaceInstruction(index + 5, new BuilderInstruction22t(Opcode.IF_EQ, 8, 9, status));
        }

        // Both scratch locals are overwritten before their next use after :goto_197; no handlers
        // exist.
        changed[0]++;

        return passionCopy(m, b);
    }

    private static final String DC = "Lcom/android/server/wm/DisplayContent;",
            WMS = "Lcom/android/server/wm/WindowManagerService;",
            RES = "Landroid/content/res/Resources;";

    private static List<Instruction> statusResourceBlock() {
        return List.of(
                new ImmutableInstruction12x(Opcode.MOVE_OBJECT, 0, 1),
                new ImmutableInstruction22c(
                        Opcode.IGET_OBJECT,
                        7,
                        9,
                        new ImmutableFieldReference(TYPE, "mDisplayContent", DC)),
                new ImmutableInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        1,
                        7,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(DC, "getDisplayId", List.of(), "I")),
                new ImmutableInstruction11x(Opcode.MOVE_RESULT, 7),
                new ImmutableInstruction11n(Opcode.CONST_4, 8, 1),
                new ImmutableInstruction22t(Opcode.IF_NE, 7, 8, 16),
                new ImmutableInstruction22c(
                        Opcode.IGET_OBJECT,
                        0,
                        9,
                        new ImmutableFieldReference(TYPE, "mService", WMS)),
                new ImmutableInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        1,
                        0,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                WMS, "getDefaultDisplayContentLocked", List.of(), DC)),
                new ImmutableInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0),
                new ImmutableInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        1,
                        0,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(DC, "getDisplayPolicy", List.of(), TYPE)),
                new ImmutableInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0),
                new ImmutableInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        1,
                        0,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                TYPE, "getCurrentUserResources", List.of(), RES)),
                new ImmutableInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0));
    }

    private static Method passionConfiguration(Method m, boolean inverse, int[] changed)
            throws Exception {
        if (!m.getName().equals("onConfigurationChanged")) {
            return m;
        }

        require(
                m.getParameterTypes().isEmpty()
                        && m.getReturnType().equals("V")
                        && m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == 10
                        && m.getImplementation().getTryBlocks().isEmpty());

        var b = new MutableMethodImplementation(m.getImplementation());
        int address = 0, index = -1;

        for (int i = 0; i < b.getInstructions().size(); i++) {
            if (address == 0x28) {
                index = i;
                break;
            }

            address += b.getInstructions().get(i).getCodeUnits();
        }

        require(index > 0);
        var expected = statusResourceBlock();

        if (inverse) {
            require(index + expected.size() < b.getInstructions().size());

            for (int i = 0; i < expected.size(); i++) {
                require(
                        FrameworkDexMaterializer.semantic(expected.get(i))
                                .equals(
                                        FrameworkDexMaterializer.semantic(
                                                b.getInstructions().get(index + i))));
            }

            for (int i = 0; i < expected.size(); i++) {
                b.removeInstruction(index);
            }
        }

        retargetDimensionCalls(b, inverse);

        if (!inverse) {
            // v0 rotation is dead after 0x16; v7/v8 are redefined at original 0x28/0x2a; no
            // handlers.
            b.addInstruction(index, new BuilderInstruction12x(Opcode.MOVE_OBJECT, 0, 1));
            b.addInstruction(
                    index + 1,
                    new BuilderInstruction22c(
                            Opcode.IGET_OBJECT,
                            7,
                            9,
                            new ImmutableFieldReference(TYPE, "mDisplayContent", DC)));
            b.addInstruction(
                    index + 2,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_VIRTUAL,
                            1,
                            7,
                            0,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(DC, "getDisplayId", List.of(), "I")));
            b.addInstruction(index + 3, new BuilderInstruction11x(Opcode.MOVE_RESULT, 7));
            b.addInstruction(index + 4, new BuilderInstruction11n(Opcode.CONST_4, 8, 1));
            b.addInstruction(index + 5, new BuilderInstruction10x(Opcode.NOP));
            b.addInstruction(
                    index + 6,
                    new BuilderInstruction22c(
                            Opcode.IGET_OBJECT,
                            0,
                            9,
                            new ImmutableFieldReference(TYPE, "mService", WMS)));
            b.addInstruction(
                    index + 7,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_VIRTUAL,
                            1,
                            0,
                            0,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    WMS, "getDefaultDisplayContentLocked", List.of(), DC)));
            b.addInstruction(index + 8, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0));
            b.addInstruction(
                    index + 9,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_VIRTUAL,
                            1,
                            0,
                            0,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(DC, "getDisplayPolicy", List.of(), TYPE)));
            b.addInstruction(index + 10, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0));
            b.addInstruction(
                    index + 11,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_VIRTUAL,
                            1,
                            0,
                            0,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    TYPE, "getCurrentUserResources", List.of(), RES)));
            b.addInstruction(index + 12, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0));
            b.replaceInstruction(
                    index + 5,
                    new BuilderInstruction22t(Opcode.IF_NE, 7, 8, b.newLabelForIndex(index + 13)));
        }

        changed[1]++;

        return passionCopy(m, b);
    }

    // Swaps the Resources receiver of both status-bar dimension reads.
    private static void retargetDimensionCalls(MutableMethodImplementation b, boolean inverse) {
        int address = 0;
        int calls = 0;

        for (int i = 0; i < b.getInstructions().size(); i++) {
            var old = b.getInstructions().get(i);

            if (address == 0x2d || address == 0x3a) {
                require(
                        old.getOpcode() == Opcode.INVOKE_VIRTUAL
                                && old
                                        instanceof
                                        org.jf.dexlib2.iface.instruction.FiveRegisterInstruction);
                var invoke = (org.jf.dexlib2.iface.instruction.FiveRegisterInstruction) old;
                require(
                        invoke.getRegisterCount() == 2
                                && invoke.getRegisterC() == (inverse ? 0 : 1)
                                && invoke.getRegisterD() == 8
                                && ((ReferenceInstruction) old)
                                        .getReference()
                                        .toString()
                                        .equals(RES + "->getDimensionPixelSize(I)I"));
                b.replaceInstruction(
                        i,
                        new BuilderInstruction35c(
                                Opcode.INVOKE_VIRTUAL,
                                2,
                                inverse ? 1 : 0,
                                8,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        RES, "getDimensionPixelSize", List.of("I"), "I")));
                calls++;
            }

            address += old.getCodeUnits();
        }

        require(calls == 2);
    }

    private static ImmutableDexFile passionRewrite(DexFile source, boolean inverse)
            throws Exception {
        var out = new ArrayList<ClassDef>();
        int[] changed = {0, 0};
        int found = 0;

        for (var c : source.getClasses()) {
            if (!c.getType().equals(TYPE)) {
                out.add(c);
                continue;
            }

            found++;
            var direct = new ArrayList<Method>();

            for (var m : c.getDirectMethods()) {
                direct.add(passionMethod(m, inverse, changed));
            }

            var virtual = new ArrayList<Method>();

            for (var m : c.getVirtualMethods()) {
                virtual.add(passionConfiguration(m, inverse, changed));
            }

            out.add(
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

        require(found == 1 && changed[0] == 1 && changed[1] == 1);

        return new ImmutableDexFile(Opcodes.forApi(30), out);
    }

    static ImmutableDexFile restorePassion(DexFile source) throws Exception {
        return rewrite(passionRewrite(source, true), true);
    }

    static byte[] apply(int api, Path original, byte[] dex, boolean passion) throws Exception {
        if (!passion) {
            return apply(api, original, dex);
        }

        byte[] nav = apply(api, original, dex);
        var expected = passionRewrite(new DexBackedDexFile(Opcodes.forApi(30), nav), false);
        byte[] out = FrameworkDexMaterializer.serialize(expected);
        var actual = new DexBackedDexFile(Opcodes.forApi(30), out);
        require(
                FrameworkDexMaterializer.semantic(expected.getClasses())
                        .equals(FrameworkDexMaterializer.semantic(actual.getClasses())));
        var before = new DexBackedDexFile(Opcodes.forApi(30), dex);
        require(
                FrameworkDexMaterializer.semantic(before.getClasses())
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        restorePassion(actual).getClasses())));

        return out;
    }
}
