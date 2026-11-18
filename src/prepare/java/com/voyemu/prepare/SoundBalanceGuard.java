package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11n;
import org.jf.dexlib2.builder.instruction.BuilderInstruction12x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22t;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.ThreeRegisterInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;

import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;

/** Guard the OEM consumer when the audio service reports an unavailable balance. */
final class SoundBalanceGuard {
    static final String OWNER = "Lcom/qinggan/app/setting/module/sound/presenter/SoundPresenter;";
    static final String METHOD = "lambda$initBalance$15$SoundPresenter";

    private static void check(boolean value) {
        Failure.require(value, "SOUND_BALANCE", "Expected OEM balance consumer shape", 3);
    }

    private static boolean target(Method m) {
        return m.getDefiningClass().equals(OWNER)
                && m.getName().equals(METHOD)
                && m.getParameterTypes().isEmpty()
                && m.getReturnType().equals("V");
    }

    private static void validateMethod(Method m) {
        var b = m.getImplementation();

        check(b != null && b.getRegisterCount() == 6 && b.getTryBlocks().isEmpty());
        var x = new ArrayList<Instruction>();
        b.getInstructions().forEach(x::add);
        check(x.size() >= 10);
        validateBalanceRead(x);
        check(
                x.get(4).getOpcode() == Opcode.CONST_4
                        && ((OneRegisterInstruction) x.get(4)).getRegisterA() == 1
                        && ((NarrowLiteralInstruction) x.get(4)).getNarrowLiteral() == 1);
        check(
                x.get(5).getOpcode() == Opcode.AGET
                        && ((ThreeRegisterInstruction) x.get(5)).getRegisterA() == 1
                        && ((ThreeRegisterInstruction) x.get(5)).getRegisterB() == 0
                        && ((ThreeRegisterInstruction) x.get(5)).getRegisterC() == 1);
        check(
                x.get(6).getOpcode() == Opcode.CONST_4
                        && ((OneRegisterInstruction) x.get(6)).getRegisterA() == 2
                        && ((NarrowLiteralInstruction) x.get(6)).getNarrowLiteral() == 0);
        check(
                x.get(7).getOpcode() == Opcode.AGET
                        && ((ThreeRegisterInstruction) x.get(7)).getRegisterA() == 0
                        && ((ThreeRegisterInstruction) x.get(7)).getRegisterB() == 0
                        && ((ThreeRegisterInstruction) x.get(7)).getRegisterC() == 2);
        check(x.get(x.size() - 1).getOpcode() == Opcode.RETURN_VOID);
        int start = 0, end = 0;

        for (int i = 0; i < x.size() - 1; i++) {
            if (i < 3) {
                start += x.get(i).getCodeUnits();
            }

            end += x.get(i).getCodeUnits();
        }

        check(start + ((OffsetInstruction) x.get(3)).getCodeOffset() == end);
    }

    // Checks the balance array read and its null branch at instructions 0-3.
    private static void validateBalanceRead(List<Instruction> x) {
        check(
                x.get(0).getOpcode() == Opcode.IGET_OBJECT
                        && ((ReferenceInstruction) x.get(0))
                                .getReference()
                                .toString()
                                .equals(
                                        OWNER
                                                + "->mAudioPolicyManager:Lcom/qinggan/audiopolicy/AudioPolicyManager;"));
        check(
                ((TwoRegisterInstruction) x.get(0)).getRegisterA() == 0
                        && ((TwoRegisterInstruction) x.get(0)).getRegisterB() == 5);
        check(
                x.get(1).getOpcode() == Opcode.INVOKE_VIRTUAL
                        && ((ReferenceInstruction) x.get(1))
                                .getReference()
                                .toString()
                                .equals(
                                        "Lcom/qinggan/audiopolicy/AudioPolicyManager;->getBalanceFaderLevel()[I"));
        check(
                ((FiveRegisterInstruction) x.get(1)).getRegisterCount() == 1
                        && ((FiveRegisterInstruction) x.get(1)).getRegisterC() == 0);
        check(
                x.get(2).getOpcode() == Opcode.MOVE_RESULT_OBJECT
                        && ((OneRegisterInstruction) x.get(2)).getRegisterA() == 0);
        check(
                x.get(3).getOpcode() == Opcode.IF_EQZ
                        && ((OneRegisterInstruction) x.get(3)).getRegisterA() == 0);
    }

    static void validate(String profile, int api, byte[] dex) {
        check("free-11-chinese-20260731-7.3.1".equals(profile) && api == 30 && dex != null);
        int count = 0;

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            for (var m : c.getVirtualMethods()) {
                if (target(m)) {
                    validateMethod(m);
                    count++;
                }
            }
        }

        check(count == 1);
    }

    static byte[] apply(String profile, int api, byte[] dex) throws Exception {
        validate(profile, api, dex);
        var all = new TreeMap<String, ClassDef>();
        int count = 0;

        for (ClassDef c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            if (c.getType().equals(OWNER)) {
                var methods = new ArrayList<Method>();

                for (var m : c.getVirtualMethods()) {
                    if (target(m)) {
                        var b = new MutableMethodImplementation(m.getImplementation());
                        int end = 0;

                        for (int i = 0; i < b.getInstructions().size() - 1; i++) {
                            end += b.getInstructions().get(i).getCodeUnits();
                        }

                        var done = b.newLabelForAddress(end);
                        b.addInstruction(4, new BuilderInstruction12x(Opcode.ARRAY_LENGTH, 1, 0));
                        b.addInstruction(5, new BuilderInstruction11n(Opcode.CONST_4, 2, 2));
                        b.addInstruction(6, new BuilderInstruction22t(Opcode.IF_LT, 1, 2, done));
                        m =
                                new ImmutableMethod(
                                        m.getDefiningClass(),
                                        m.getName(),
                                        m.getParameters(),
                                        m.getReturnType(),
                                        m.getAccessFlags(),
                                        m.getAnnotations(),
                                        m.getHiddenApiRestrictions(),
                                        b);
                        count++;
                    }

                    methods.add(m);
                }

                c =
                        new ImmutableClassDef(
                                c.getType(),
                                c.getAccessFlags(),
                                c.getSuperclass(),
                                c.getInterfaces(),
                                c.getSourceFile(),
                                c.getAnnotations(),
                                c.getStaticFields(),
                                c.getInstanceFields(),
                                c.getDirectMethods(),
                                methods);
            }

            all.put(c.getType(), c);
        }

        check(count == 1);

        return FrameworkDexMaterializer.serialize(
                new ImmutableDexFile(Opcodes.forApi(30), all.values()));
    }
}
