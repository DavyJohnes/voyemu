package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21s;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22t;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.RegisterRangeInstruction;
import org.jf.dexlib2.iface.instruction.ThreeRegisterInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;

import java.util.ArrayList;
import java.util.List;

/** An unavailable instrument-card sentinel is not a six-bit card selection. */
final class PassionVehicleInstrumentGuard {
    static final String OWNER = "Lcom/voyah/car/ui/common/display/InstrumentFragmentN;",
            METHOD = "refreshAdapter";

    private static void require(boolean value) {
        Failure.require(
                value,
                "PASSION_VEHICLE_INSTRUMENT_CONTRACT",
                "Exact owner-pinned six-bit instrument card path required",
                3);
    }

    static Method method(DexFile dex) {
        Method found = null;

        for (var c : dex.getClasses()) {
            if (c.getType().equals(OWNER)) {
                for (var m : c.getDirectMethods()) {
                    if (m.getName().equals(METHOD)) {
                        require(found == null);
                        found = m;
                    }
                }
            }
        }

        require(found != null);

        return found;
    }

    // Check only the pinned straight-line prefix: a scratch value must be overwritten before any
    // read.
    static void validateScratch(Method m, int scratch) {
        int at = 0;

        for (var i : m.getImplementation().getInstructions()) {
            if (at >= 43) {
                require(!(i instanceof OffsetInstruction));
                var reads = new ArrayList<Integer>();
                boolean writes = i.getOpcode().setsRegister();

                if (i instanceof FiveRegisterInstruction r) {
                    int[] regs = {
                        r.getRegisterC(),
                        r.getRegisterD(),
                        r.getRegisterE(),
                        r.getRegisterF(),
                        r.getRegisterG()
                    };

                    for (int n = 0; n < r.getRegisterCount(); n++) {
                        reads.add(regs[n]);
                    }
                } else if (i instanceof RegisterRangeInstruction r) {
                    for (int n = 0; n < r.getRegisterCount(); n++) {
                        reads.add(r.getStartRegister() + n);
                    }
                } else {
                    if (i instanceof OneRegisterInstruction r && !writes) {
                        reads.add(r.getRegisterA());
                    }

                    if (i instanceof TwoRegisterInstruction r) {
                        reads.add(r.getRegisterB());
                    }

                    if (i instanceof ThreeRegisterInstruction r) {
                        reads.add(r.getRegisterC());
                    }
                }

                require(!reads.contains(scratch));

                if (writes
                        && i instanceof OneRegisterInstruction r
                        && r.getRegisterA() == scratch) {
                    return;
                }
            }

            at += i.getCodeUnits();
        }

        require(false);
    }

    static void validate(DexFile dex) {
        var m = method(dex);

        validateScratch(m, 3);
        require(
                m.getAccessFlags() == 2
                        && m.getParameterTypes().equals(List.of("Ljava/lang/Integer;"))
                        && m.getReturnType().equals("V"));
        var b = m.getImplementation();
        require(b != null && b.getRegisterCount() == 8 && b.getTryBlocks().isEmpty());
        int at = 0, checked = 0;

        for (var i : b.getInstructions()) {
            if (requireCardRead(i, at) || requireCardSelection(i, at)) {
                checked++;
            }

            at += i.getCodeUnits();
        }

        require(at == 188 && checked == 8);
    }

    private static boolean requireCardRead(Instruction i, int at) {
        switch (at) {
            case 24 ->
                    require(
                            i.getOpcode() == Opcode.IF_NEZ
                                    && ((OneRegisterInstruction) i).getRegisterA() == 7
                                    && ((OffsetInstruction) i).getCodeOffset() == 3);
            case 39 ->
                    require(
                            i.getOpcode() == Opcode.INVOKE_VIRTUAL
                                    && ((ReferenceInstruction) i)
                                            .getReference()
                                            .toString()
                                            .equals("Ljava/lang/Integer;->intValue()I")
                                    && ((FiveRegisterInstruction) i).getRegisterC() == 7);
            case 42 ->
                    require(
                            i.getOpcode() == Opcode.MOVE_RESULT
                                    && ((OneRegisterInstruction) i).getRegisterA() == 7);
            case 43 ->
                    require(
                            i.getOpcode() == Opcode.INVOKE_STATIC
                                    && ((ReferenceInstruction) i)
                                            .getReference()
                                            .toString()
                                            .equals(
                                                    "Ljava/lang/Integer;->toBinaryString(I)Ljava/lang/String;")
                                    && ((FiveRegisterInstruction) i).getRegisterC() == 7);
            default -> {
                return false;
            }
        }

        return true;
    }

    private static boolean requireCardSelection(Instruction i, int at) {
        switch (at) {
            case 55 ->
                    require(
                            i.getOpcode() == Opcode.MOVE_RESULT
                                    && ((OneRegisterInstruction) i).getRegisterA() == 3);
            case 98 ->
                    require(
                            i.getOpcode() == Opcode.INVOKE_STATIC
                                    && ((ReferenceInstruction) i)
                                            .getReference()
                                            .toString()
                                            .equals(
                                                    "Lcom/voyah/common/log/LogUtils;->d(Ljava/lang/String;Ljava/lang/String;)V")
                                    && ((FiveRegisterInstruction) i).getRegisterC() == 1
                                    && ((FiveRegisterInstruction) i).getRegisterD() == 7);
            case 111 ->
                    require(
                            i.getOpcode() == Opcode.INVOKE_DIRECT
                                    && ((ReferenceInstruction) i)
                                            .getReference()
                                            .toString()
                                            .equals(
                                                    OWNER
                                                            + "->reflectionInstrumentCardId(Ljava/lang/String;[I)Ljava/lang/String;"));
            case 171 -> require(i.getOpcode() == Opcode.RETURN_VOID);
            default -> {
                return false;
            }
        }

        return true;
    }

    static Method rewriteMethod(Method m, boolean inverse) {
        var body = new MutableMethodImplementation(m.getImplementation());
        int address = 0, index = -1;

        for (int i = 0; i < body.getInstructions().size(); i++) {
            if (address == 43) {
                index = i;
            }

            address += body.getInstructions().get(i).getCodeUnits();
        }

        require(index >= 0 && address == (inverse ? 194 : 188));

        if (inverse) {
            var a = body.getInstructions().get(index);
            var b = body.getInstructions().get(index + 1);
            var c = body.getInstructions().get(index + 2);
            require(
                    a.getOpcode() == Opcode.IF_LTZ
                            && ((OneRegisterInstruction) a).getRegisterA() == 7
                            && ((OffsetInstruction) a).getCodeOffset() == 134);
            require(
                    b.getOpcode() == Opcode.CONST_16
                            && ((OneRegisterInstruction) b).getRegisterA() == 3
                            && ((NarrowLiteralInstruction) b).getNarrowLiteral() == 64);
            require(
                    c.getOpcode() == Opcode.IF_GE
                            && ((TwoRegisterInstruction) c).getRegisterA() == 7
                            && ((TwoRegisterInstruction) c).getRegisterB() == 3
                            && ((OffsetInstruction) c).getCodeOffset() == 130);

            for (int n = 0; n < 3; n++) {
                body.removeInstruction(index);
            }
        } else {
            var end = body.newLabelForAddress(171);
            body.addInstruction(index, new BuilderInstruction21t(Opcode.IF_LTZ, 7, end));
            body.addInstruction(index + 1, new BuilderInstruction21s(Opcode.CONST_16, 3, 64));
            body.addInstruction(index + 2, new BuilderInstruction22t(Opcode.IF_GE, 7, 3, end));
        }

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                body);
    }

    static ImmutableDexFile rewrite(DexFile dex, boolean inverse) {
        var classes = new ArrayList<ClassDef>();
        int changed = 0;

        for (var c : dex.getClasses()) {
            if (!c.getType().equals(OWNER)) {
                classes.add(c);
                continue;
            }

            var direct = new ArrayList<Method>();

            for (var m : c.getDirectMethods()) {
                if (m.getName().equals(METHOD)) {
                    m = rewriteMethod(m, inverse);
                    changed++;
                }

                direct.add(m);
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
                            c.getVirtualMethods()));
        }

        require(changed == 1);

        return new ImmutableDexFile(Opcodes.forApi(30), classes);
    }
}
