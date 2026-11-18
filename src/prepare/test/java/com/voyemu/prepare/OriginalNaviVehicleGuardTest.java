package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public class OriginalNaviVehicleGuardTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    static void check(boolean x, String s) {
        if (!x) {
            throw new AssertionError(s);
        }
    }

    public static void main(String[] a) throws Exception {
        byte[] orig = Files.readAllBytes(Path.of(a[0])),
                out = OriginalNaviVehicleGuard.apply(PROFILE, 30, "arm64-v8a", Path.of(a[2]), orig);
        Method
                old =
                        OriginalNaviVehicleGuard.method(
                                OriginalNaviVehicleGuard.classes(orig)
                                        .get(OriginalNaviVehicleGuard.OWNER)),
                now =
                        OriginalNaviVehicleGuard.method(
                                OriginalNaviVehicleGuard.classes(out)
                                        .get(OriginalNaviVehicleGuard.OWNER));
        List<Instruction> x = new ArrayList<>(), y = new ArrayList<>();

        old.getImplementation().getInstructions().forEach(x::add);
        now.getImplementation().getInstructions().forEach(y::add);
        checkPatchedShape(x, y);

        for (byte[] b : List.of(out, Arrays.copyOf(orig, orig.length - 1))) {
            check(rejects(PROFILE, 30, "arm64-v8a", Path.of(a[2]), b), "bad dex accepted");
        }

        check(rejects("other", 30, "arm64-v8a", Path.of(a[2]), orig), "bad profile");
        check(rejects(PROFILE, 29, "arm64-v8a", Path.of(a[2]), orig), "bad API");
        check(rejects(PROFILE, 30, "x86_64", Path.of(a[2]), orig), "bad ABI");
        check(rejects(PROFILE, 30, "arm64-v8a", Path.of(a[0]), orig), "bad JAR");
        System.out.println(
                "PASS cached return, branch targets, OEM fallback, wrong pin/reapply and metadata");
    }

    static boolean rejects(String profile, int api, String abi, Path jar, byte[] dex)
            throws Exception {
        try {
            OriginalNaviVehicleGuard.apply(profile, api, abi, jar, dex);
        } catch (Failure e) {
            return true;
        }

        return false;
    }

    static void checkPatchedShape(List<Instruction> x, List<Instruction> y) throws Exception {
        check(y.size() == x.size() + 5, "size");
        check(
                y.get(0).getOpcode() == Opcode.SGET
                        && y.get(1).getOpcode() == Opcode.IF_EQZ
                        && y.get(2).getOpcode() == Opcode.RETURN,
                "cached return");
        check(
                y.get(3) instanceof ReferenceInstruction
                        && ((ReferenceInstruction) y.get(3))
                                .getReference()
                                .toString()
                                .equals(OriginalNaviVehicleGuard.HELPER + "->lookup()I"),
                "helper");
        check(
                y.get(4).getOpcode() == Opcode.MOVE_RESULT
                        && y.get(5).getOpcode() == Opcode.IF_EQZ
                        && y.get(6).getOpcode() == Opcode.SPUT
                        && y.get(7).getOpcode() == Opcode.RETURN
                        && y.get(8).getOpcode() == Opcode.CONST_16,
                "branch shape");
        int pc = 0, first = -1, second = -1, helper = -1, original = -1;

        for (int i = 0; i < y.size(); i++) {
            if (i == 1) {
                first = pc + ((OffsetInstruction) y.get(i)).getCodeOffset();
            }

            if (i == 3) {
                helper = pc;
            }

            if (i == 5) {
                second = pc + ((OffsetInstruction) y.get(i)).getCodeOffset();
            }

            if (i == 8) {
                original = pc;
            }

            pc += y.get(i).getCodeUnits();
        }

        check(first == helper && second == original, "branch targets");

        for (int i = 3; i < x.size(); i++) {
            check(
                    FrameworkDexMaterializer.semantic(x.get(i))
                            .equals(FrameworkDexMaterializer.semantic(y.get(i + 5))),
                    "original branch drift at " + i);
        }
    }
}
