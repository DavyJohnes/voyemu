package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public class OriginalNaviVinGuardTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    static void check(boolean x, String s) {
        if (!x) {
            throw new AssertionError(s);
        }
    }

    public static void main(String[] a) throws Exception {
        byte[] raw = Files.readAllBytes(Path.of(a[0])),
                patched = OriginalNaviVinGuard.apply(PROFILE, 30, "arm64-v8a", Path.of(a[2]), raw);
        Method
                o =
                        OriginalNaviVinGuard.method(
                                OriginalNaviVinGuard.classes(raw).get(OriginalNaviVinGuard.OWNER)),
                m =
                        OriginalNaviVinGuard.method(
                                OriginalNaviVinGuard.classes(patched)
                                        .get(OriginalNaviVinGuard.OWNER));
        List<Instruction> x = new ArrayList<>(), y = new ArrayList<>();

        o.getImplementation().getInstructions().forEach(x::add);
        m.getImplementation().getInstructions().forEach(y::add);
        check(y.size() == x.size() + 2, "instruction count");

        for (int i = 0; i < x.size() - 1; i++) {
            check(
                    FrameworkDexMaterializer.semantic(x.get(i))
                            .equals(FrameworkDexMaterializer.semantic(y.get(i))),
                    "OEM instruction drift " + i);
        }

        int last = x.size() - 1;
        check(
                y.get(last) instanceof ReferenceInstruction
                        && ((ReferenceInstruction) y.get(last))
                                .getReference()
                                .toString()
                                .equals(
                                        OriginalNaviVinGuard.HELPER
                                                + "->select(Ljava/lang/String;)Ljava/lang/String;"),
                "fallback reference");
        check(
                y.get(last + 1).getOpcode() == Opcode.MOVE_RESULT_OBJECT
                        && y.get(last + 2).getOpcode() == Opcode.RETURN_OBJECT,
                "return sequence");
        int off = 0, branch = -1, empty = -1;

        for (int i = 0; i < y.size(); i++) {
            if (y.get(i).getOpcode() == Opcode.IF_EQZ) {
                branch = off + ((OffsetInstruction) y.get(i)).getCodeOffset();
            }

            if (i == last - 1) {
                empty = off;
            }

            off += y.get(i).getCodeUnits();
        }

        check(branch == empty, "invalid VIN branch misses fallback");
        check(y.get(last - 2).getOpcode() == Opcode.RETURN_OBJECT, "valid original return altered");
        boolean rejected = false;

        try {
            OriginalNaviVinGuard.apply(PROFILE, 30, "arm64-v8a", Path.of(a[2]), patched);
        } catch (Failure e) {
            rejected = true;
        }

        check(rejected, "reapply accepted");
        rejected = false;

        try {
            OriginalNaviVinGuard.apply("wrong-profile", 30, "arm64-v8a", Path.of(a[2]), raw);
        } catch (Failure e) {
            rejected = true;
        }

        check(rejected, "wrong profile accepted");
        rejected = false;

        try {
            OriginalNaviVinGuard.apply(PROFILE, 30, "arm64-v8a", Path.of(a[0]), raw);
        } catch (Failure e) {
            rejected = true;
        }

        check(rejected, "wrong jar accepted");
        System.out.println(
                "PASS OEM branch paths, exact return sequence, wrong pin, reapply, full semantic"
                        + " and raw flags");
    }
}
