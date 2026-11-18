package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.BuilderInstruction;
import org.jf.dexlib2.builder.BuilderOffsetInstruction;
import org.jf.dexlib2.builder.BuilderTryBlock;
import org.jf.dexlib2.builder.Label;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/**
 * Exact pinned OEM screen sender hooks; helper and genuine CSVs are ordinary recipe dependencies.
 */
public final class PassionNexusPowerGuard {
    static final String NAME = "passion-api30-nexus-power";
    static final String PROFILE = "passion-11-russian-20260403-2.1.4.80",
            PIN = "603805fbc8aa50ff473cd34d2082366bd5cfa6ed9439f37188e9301e1b812b11",
            OWNER = "Lcom/mega/nexus/server/ScreenStateProcessor;",
            ROUTER = "Lcom/voyemu/display/OemDisplayPower;";
    static final Set<String> METHODS =
            Set.of("sendQnxScreenEvent", "getScreenState", "requestScreenOff", "requestScreenOn");

    static void check(boolean value) {
        if (!value) {
            throw new IllegalArgumentException("Exact pinned OEM screen sender contract required");
        }
    }

    static boolean target(Method m) {
        return m.getDefiningClass().equals(OWNER) && METHODS.contains(m.getName());
    }

    static BuilderInstruction35c one(String name, int register) {
        return new BuilderInstruction35c(
                Opcode.INVOKE_STATIC,
                1,
                register,
                0,
                0,
                0,
                0,
                new ImmutableMethodReference(ROUTER, name, List.of("Ljava/lang/Object;"), "V"));
    }

    static boolean helper(BuilderInstruction i, String name) {
        return i instanceof ReferenceInstruction r
                && r.getReference() instanceof org.jf.dexlib2.iface.reference.MethodReference m
                && m.getDefiningClass().equals(ROUTER)
                && m.getName().equals(name);
    }

    static Method route(Method m, boolean inverse) {
        var b = new MutableMethodImplementation(m.getImplementation());
        List<BuilderTryBlock> protectedBlocks = null;
        String name = m.getName();
        boolean sender = name.equals("sendQnxScreenEvent"), getter = name.equals("getScreenState");
        int self = sender ? 4 : getter ? 1 : 5;

        if (sender) {
            routeSender(b, inverse);
        } else {
            if (!getter) {
                routeResult(b, inverse);

                if (inverse) {
                    removeFinish(b);
                } else {
                    addFinish(b, self);
                }
            }

            protectedBlocks = routeEntry(b, getter, self, inverse);
        }

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                protectedBlocks == null
                        ? b
                        : new ImmutableMethodImplementation(
                                b.getRegisterCount(),
                                b.getInstructions(),
                                protectedBlocks,
                                b.getDebugItems()));
    }

    private static void routeSender(MutableMethodImplementation b, boolean inverse) {
        if (inverse) {
            check(
                    helper(b.getInstructions().get(0), "send")
                            && b.getInstructions().get(1).getOpcode() == Opcode.MOVE_RESULT
                            && b.getInstructions().get(2).getOpcode() == Opcode.IF_EQZ
                            && b.getInstructions().get(3).getOpcode() == Opcode.RETURN_VOID);

            for (int i = 0; i < 4; i++) {
                b.removeInstruction(0);
            }
        } else {
            Label original = b.newLabelForIndex(0);
            b.addInstruction(
                    0,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_STATIC,
                            2,
                            4,
                            5,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    ROUTER, "send", List.of("Ljava/lang/Object;", "Z"), "Z")));
            b.addInstruction(1, new BuilderInstruction11x(Opcode.MOVE_RESULT, 0));
            b.addInstruction(2, new BuilderInstruction21t(Opcode.IF_EQZ, 0, original));
            b.addInstruction(3, new BuilderInstruction10x(Opcode.RETURN_VOID));
        }
    }

    private static void routeResult(MutableMethodImplementation b, boolean inverse) {
        int result = 0;

        for (int index = 0; index < b.getInstructions().size(); index++) {
            var instruction = b.getInstructions().get(index);

            if ((!inverse
                            && instruction instanceof ReferenceInstruction r
                            && r.getReference()
                                    .toString()
                                    .equals(
                                            "Lcom/mega/nexus/os/IScreenRequestListener;->onResult(I)V"))
                    || (inverse && helper(instruction, "result"))) {
                var args = (FiveRegisterInstruction) instruction;
                check(
                        args.getRegisterCount() == (inverse ? 3 : 2)
                                && args.getRegisterC() == (inverse ? 5 : 7)
                                && args.getRegisterD() == (inverse ? 7 : 1)
                                && (!inverse || args.getRegisterE() == 1));
                b.replaceInstruction(index, resultCall(inverse));
                result++;
            }
        }

        check(result == 1);
    }

    private static BuilderInstruction35c resultCall(boolean inverse) {
        return new BuilderInstruction35c(
                inverse ? Opcode.INVOKE_INTERFACE : Opcode.INVOKE_STATIC,
                inverse ? 2 : 3,
                inverse ? 7 : 5,
                inverse ? 1 : 7,
                inverse ? 0 : 1,
                0,
                0,
                new ImmutableMethodReference(
                        inverse ? "Lcom/mega/nexus/os/IScreenRequestListener;" : ROUTER,
                        inverse ? "onResult" : "result",
                        inverse
                                ? List.of("I")
                                : List.of("Ljava/lang/Object;", "Ljava/lang/Object;", "I"),
                        "V"));
    }

    private static void removeFinish(MutableMethodImplementation b) {
        int finish = 0;

        for (int index = 0; index < b.getInstructions().size(); index++) {
            if (helper(b.getInstructions().get(index), "finish")) {
                check(
                        ((FiveRegisterInstruction) b.getInstructions().get(index)).getRegisterC()
                                == 5);
                b.removeInstruction(index);
                finish++;
            }
        }

        check(finish == 1);
    }

    // Branch targets are matched by the identity of the mutable builder location.
    @SuppressWarnings("ReferenceEquality")
    private static void addFinish(MutableMethodImplementation b, int self) {
        int exit = -1;

        for (int index = 0; index < b.getInstructions().size() - 1; index++) {
            if (b.getInstructions().get(index).getOpcode() == Opcode.MONITOR_EXIT
                    && b.getInstructions().get(index + 1).getOpcode() == Opcode.RETURN_VOID) {
                check(exit < 0);
                exit = index;
            }
        }

        check(exit >= 0);
        var oldLocation = b.getInstructions().get(exit).getLocation();
        b.addInstruction(exit, one("finish", self));
        Label newTarget = b.newLabelForIndex(exit);
        int branches = 0;

        for (int index = 0; index < b.getInstructions().size(); index++) {
            var instruction = b.getInstructions().get(index);

            if (instruction instanceof BuilderOffsetInstruction offset
                    && offset.getTarget().getLocation() == oldLocation) {
                if (instruction.getOpcode() == Opcode.GOTO) {
                    b.replaceInstruction(index, new BuilderInstruction10t(Opcode.GOTO, newTarget));
                } else {
                    check(instruction.getOpcode() == Opcode.IF_EQZ);
                    b.replaceInstruction(
                            index,
                            new BuilderInstruction21t(
                                    Opcode.IF_EQZ,
                                    ((OneRegisterInstruction) instruction).getRegisterA(),
                                    newTarget));
                }

                branches++;
            }
        }

        check(branches == 2);
    }

    // Branch targets are matched by the identity of the mutable builder location.
    @SuppressWarnings("ReferenceEquality")
    private static List<BuilderTryBlock> routeEntry(
            MutableMethodImplementation b, boolean getter, int self, boolean inverse) {
        if (inverse) {
            check(helper(b.getInstructions().get(1), getter ? "bootstrap" : "before"));
            b.removeInstruction(1);

            return null;
        } else {
            check(
                    b.getInstructions().get(0).getOpcode() == Opcode.MONITOR_ENTER
                            && ((OneRegisterInstruction) b.getInstructions().get(0)).getRegisterA()
                                    == self);
            var oldStart = b.getInstructions().get(1).getLocation();
            b.addInstruction(1, one(getter ? "bootstrap" : "before", self));
            Label protectedStart = b.newLabelForIndex(1);
            var blocks = new ArrayList<>(b.getTryBlocks());

            for (int i = 0; i < blocks.size(); i++) {
                var block = blocks.get(i);

                if (block.start.getLocation() == oldStart) {
                    blocks.set(
                            i,
                            new BuilderTryBlock(
                                    protectedStart,
                                    block.end,
                                    block.exceptionHandler.getExceptionType(),
                                    block.exceptionHandler.getHandler()));
                }
            }

            return blocks;
        }
    }

    static ImmutableDexFile rewrite(DexFile dex, boolean inverse) {
        List<ClassDef> classes = new ArrayList<>();
        int count = 0;

        for (var c : dex.getClasses()) {
            List<Method> direct = new ArrayList<>(), virtual = new ArrayList<>();

            for (var m : c.getDirectMethods()) {
                if (target(m)) {
                    m = route(m, inverse);
                    count++;
                }

                direct.add(m);
            }

            for (var m : c.getVirtualMethods()) {
                if (target(m)) {
                    m = route(m, inverse);
                    count++;
                }

                virtual.add(m);
            }

            classes.add(
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

        check(count == 4);

        return new ImmutableDexFile(Opcodes.forApi(30), classes);
    }

    static void validate(String profile, int api, Path apk, byte[] dex) throws Exception {
        check(
                PROFILE.equals(profile)
                        && api == 30
                        && FilesSupport.hash(apk).equals(PIN)
                        && dex != null);
        var original = new DexBackedDexFile(Opcodes.forApi(30), dex);
        int selected = 0;

        for (var c : original.getClasses()) {
            for (var m : c.getMethods()) {
                if (target(m)) {
                    check(m.getImplementation() != null);
                    selected++;
                }
            }
        }

        check(selected == 4);
    }

    static byte[] apply(String profile, int api, Path apk, byte[] dex) throws Exception {
        validate(profile, api, apk, dex);
        var original = new DexBackedDexFile(Opcodes.forApi(30), dex);
        byte[] out = FrameworkDexMaterializer.serialize(rewrite(original, false));
        var restored =
                new DexBackedDexFile(
                        Opcodes.forApi(30),
                        FrameworkDexMaterializer.serialize(
                                rewrite(new DexBackedDexFile(Opcodes.forApi(30), out), true)));
        Map<String, ClassDef> classes = new TreeMap<>();

        for (var c : restored.getClasses()) {
            classes.put(c.getType(), c);
        }

        check(classes.size() == original.getClasses().size());

        for (var c : original.getClasses()) {
            if (!FrameworkDexMaterializer.semantic(c)
                    .equals(FrameworkDexMaterializer.semantic(classes.get(c.getType())))) {
                System.out.println("inverse mismatch " + c.getType());
                Map<String, Method> after = new TreeMap<>();

                for (var method : classes.get(c.getType()).getMethods()) {
                    after.put(method.toString(), method);
                }

                for (var method : c.getMethods()) {
                    if (!FrameworkDexMaterializer.semantic(method)
                            .equals(
                                    FrameworkDexMaterializer.semantic(
                                            after.get(method.toString())))) {
                        System.out.println(
                                "method "
                                        + method
                                        + " original="
                                        + FrameworkDexMaterializer.semantic(method)
                                        + " restored="
                                        + FrameworkDexMaterializer.semantic(
                                                after.get(method.toString())));

                        for (var tb : method.getImplementation().getTryBlocks()) {
                            System.out.println(
                                    "original try "
                                            + tb.getStartCodeAddress()
                                            + " "
                                            + tb.getCodeUnitCount()
                                            + " "
                                            + tb.getExceptionHandlers());
                        }

                        for (var tb :
                                after.get(method.toString()).getImplementation().getTryBlocks()) {
                            System.out.println(
                                    "restored try "
                                            + tb.getStartCodeAddress()
                                            + " "
                                            + tb.getCodeUnitCount()
                                            + " "
                                            + tb.getExceptionHandlers());
                        }
                    }
                }

                check(false);
            }
        }

        return out;
    }
}
