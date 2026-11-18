package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;

/** Execute the real getter suffix, including its original empty-list branch. */
public final class PhoneDisconnectGuardTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    static void need(boolean ok, String message) {
        if (!ok) {
            throw new AssertionError(message);
        }
    }

    static Object suffix(byte[] dex, Object field) {
        Method method = null;

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            for (var m : c.getMethods()) {
                if (m.toString().equals(PhoneDisconnectGuard.METHOD)) {
                    method = m;
                }
            }
        }

        need(method != null, "getter missing");
        var code = new TreeMap<Integer, org.jf.dexlib2.iface.instruction.Instruction>();
        int at = 0;

        for (var ins : method.getImplementation().getInstructions()) {
            code.put(at, ins);
            at += ins.getCodeUnits();
        }

        int pc = code.get(0x5b).getOpcode() == Opcode.INVOKE_STATIC ? 0x5e : 0x5b;
        Object value = null;

        for (int steps = 0; steps < 8; steps++) {
            var ins = code.get(pc);
            need(ins != null, "invalid branch boundary " + pc);

            switch (ins.getOpcode()) {
                case IGET_OBJECT -> {
                    need(((OneRegisterInstruction) ins).getRegisterA() == 0, "field register");
                    value = field;
                }
                case IF_EQZ -> {
                    need(((OneRegisterInstruction) ins).getRegisterA() == 0, "branch register");

                    if (value == null) {
                        pc += ((OffsetInstruction) ins).getCodeOffset();
                        continue;
                    }
                }
                case NEW_INSTANCE -> {
                    need(
                            ((ReferenceInstruction) ins)
                                    .getReference()
                                    .toString()
                                    .equals("Ljava/util/ArrayList;"),
                            "empty class");
                    value = new ArrayList<>();
                }
                case INVOKE_DIRECT -> {
                    need(
                            ((ReferenceInstruction) ins)
                                    .getReference()
                                    .toString()
                                    .equals("Ljava/util/ArrayList;-><init>()V"),
                            "empty constructor");
                }
                case RETURN_OBJECT -> {
                    need(((OneRegisterInstruction) ins).getRegisterA() == 0, "return register");

                    return value;
                }
                default -> throw new AssertionError("unexpected " + ins.getOpcode() + " at " + pc);
            }

            pc += ins.getCodeUnits();
        }

        throw new AssertionError("loop");
    }

    public static void main(String[] args) throws Exception {
        Path apk = Path.of(args[0]);
        byte[] original = NativeCore.entries(apk).get("classes.dex");
        byte[] window = PhoneCallWindowGuard.apply(PROFILE, 30, apk, original);
        Object oldNull = suffix(window, null);

        need(oldNull == null, "RED: original returns null after disconnect");
        byte[] fixed = PhoneDisconnectGuard.apply(PROFILE, 30, apk, window);
        Object empty = suffix(fixed, null);
        need(empty instanceof List && ((List<?>) empty).isEmpty(), "null must yield empty list");
        var nonempty = new ArrayList<>(List.of("call"));
        var alreadyEmpty = new ArrayList<>();
        // The fixed getter must return the caller's snapshot objects themselves.
        @SuppressWarnings("ReferenceEquality")
        boolean retained = suffix(fixed, nonempty) == nonempty,
                retainedEmpty = suffix(fixed, alreadyEmpty) == alreadyEmpty;
        need(retained, "return original snapshot");
        need(retainedEmpty, "retain empty snapshot");
        PhoneDisconnectGuard.verify(PROFILE, apk, original, fixed);
        byte[] drift = fixed.clone();
        drift[drift.length - 1] ^= 1;
        boolean rejected = false;

        try {
            PhoneDisconnectGuard.verify(PROFILE, apk, original, drift);
        } catch (Failure expected) {
            rejected = true;
        }

        need(rejected, "reject drift");
        System.out.println(
                "RED original returns null; GREEN null returns empty and non-null snapshot identity"
                        + " preserved; drift rejected");
    }
}
