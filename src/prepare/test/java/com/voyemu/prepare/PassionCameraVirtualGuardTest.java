package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;

import java.nio.file.Path;
import java.util.Map;
import java.util.TreeMap;

public final class PassionCameraVirtualGuardTest {
    interface Checked {
        void run() throws Exception;
    }

    static void rejects(Checked c) throws Exception {
        try {
            c.run();
            throw new AssertionError("invalid camera accepted");
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }
    }

    static void check(boolean b, String s) {
        if (!b) {
            throw new AssertionError(s);
        }
    }

    static int destination(Map<Integer, Instruction> code, int pc, Opcode op) {
        var i = code.get(pc);

        check(i != null && i.getOpcode() == op, "opcode at " + pc);

        return pc + ((OffsetInstruction) i).getCodeOffset();
    }

    static int runSearch(Map<Integer, Instruction> code, int requested, Integer[] cameras) {
        int loop = 11, pc = loop, index = 0, desired = requested, steps = 0;

        while (steps++ < 100) {
            if (pc == loop) {
                pc = index >= cameras.length ? destination(code, pc, Opcode.IF_GE) : 13;
            } else if (pc == 13) {
                Integer facing = cameras[index];

                if (facing == null) {
                    return -2;
                }

                pc = facing == desired ? 47 : destination(code, 45, Opcode.IF_NE);
            } else if (pc == 47) {
                return index;
            } else if (pc == 48) {
                index++;
                pc = destination(code, 50, Opcode.GOTO);
            } else if (pc == 78) {
                int external = ((NarrowLiteralInstruction) code.get(78)).getNarrowLiteral();
                pc = desired != external ? destination(code, 79, Opcode.IF_NE) : 81;
            } else if (pc == 81) {
                desired = ((NarrowLiteralInstruction) code.get(81)).getNarrowLiteral();
                index = ((NarrowLiteralInstruction) code.get(82)).getNarrowLiteral();
                pc = destination(code, 83, Opcode.GOTO_32);
            } else if (pc == 55) {
                return -1;
            } else {
                throw new AssertionError("unexpected PC " + pc);
            }
        }

        throw new AssertionError("loop");
    }

    public static void main(String[] a) throws Exception {
        Path apk = Path.of(a[0]);
        byte[] raw = NativeCore.entries(apk).get("classes2.dex");
        String p = PassionCameraVirtualGuard.PROFILE;

        PassionCameraVirtualGuard.validateOriginalChain(p, 30, apk, raw);
        byte[] prior = PassionServiceForegroundGuard.apply(p, 30, apk, raw),
                out = PassionCameraVirtualGuard.apply(p, 30, apk, prior);
        var dex = new DexBackedDexFile(Opcodes.forApi(30), out);
        Method target = null;

        for (var c : dex.getClasses()) {
            if (c.getType().equals(PassionCameraVirtualGuard.OWNER)) {
                for (var m : c.getDirectMethods()) {
                    if (PassionCameraVirtualGuard.target(m)) {
                        target = m;
                    }
                }
            }
        }

        check(target != null, "target");
        var code = new TreeMap<Integer, Instruction>();
        int pc = 0;

        for (var i : target.getImplementation().getInstructions()) {
            code.put(pc, i);
            pc += i.getCodeUnits();
        }

        check(destination(code, 11, Opcode.IF_GE) == 78, "exhaustion must enter fallback");
        check(destination(code, 79, Opcode.IF_NE) == 55, "non-external preserves OEM failure");
        check(destination(code, 83, Opcode.GOTO_32) == 11, "fallback searches full list again");
        check(
                runSearch(code, 2, new Integer[] {1, 2}) == 1,
                "BACK before EXTERNAL must preserve external priority");
        check(runSearch(code, 2, new Integer[] {1}) == 0, "only BACK is emulated fallback");
        check(runSearch(code, 2, new Integer[] {}) == -1, "empty remains unavailable");
        check(runSearch(code, 0, new Integer[] {1}) == -1, "other facing must not fall back");
        check(
                runSearch(code, 2, new Integer[] {null, 1}) == -2,
                "null characteristics must keep OEM early failure");
        rejects(() -> PassionCameraVirtualGuard.apply("wrong", 30, apk, prior));
        rejects(() -> PassionCameraVirtualGuard.apply(p, 29, apk, prior));
        rejects(() -> PassionCameraVirtualGuard.apply(p, 30, apk, out));
        System.out.println(
                "Camera actual branch targets, EXTERNAL priority, BACK, empty, other facing, error,"
                        + " inverse and negatives PASS; runtime pending");
    }
}
