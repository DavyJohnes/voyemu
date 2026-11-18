package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableExceptionHandler;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.ImmutableTryBlock;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.zip.ZipFile;

public class NaviVinGuardTest {
    static Method synthetic(
            int[][] ranges, List<org.jf.dexlib2.iface.ExceptionHandler> handlers, boolean changed) {
        var ins = new ArrayList<org.jf.dexlib2.iface.instruction.Instruction>();

        for (int i = 0; i < 8; i++) {
            ins.add(
                    new ImmutableInstruction10x(
                            changed && i == 3 ? Opcode.RETURN_VOID : Opcode.NOP));
        }

        var blocks =
                new ArrayList<
                        org.jf.dexlib2.iface.TryBlock<
                                ? extends org.jf.dexlib2.iface.ExceptionHandler>>();

        for (var r : ranges) {
            blocks.add(new ImmutableTryBlock(r[0], r[1], handlers));
        }

        return new ImmutableMethod(
                "LTest;",
                "m",
                List.of(),
                "V",
                9,
                Set.of(),
                Set.of(),
                new ImmutableMethodImplementation(0, ins, blocks, List.of()));
    }

    static List<String> coverage(Method m) throws Exception {
        var result = new ArrayList<String>();
        int address = 0;

        for (var instruction : m.getImplementation().getInstructions()) {
            var covered = new ArrayList<String>();

            for (var block : m.getImplementation().getTryBlocks()) {
                if (address >= block.getStartCodeAddress()
                        && address < block.getStartCodeAddress() + block.getCodeUnitCount()) {
                    covered.add(FrameworkDexMaterializer.semantic(block.getExceptionHandlers()));
                }
            }

            result.add(covered.toString());
            address += instruction.getCodeUnits();
        }

        return result;
    }

    static void assertVinPaths(Method m) {
        var ins = new ArrayList<Instruction>();

        m.getImplementation().getInstructions().forEach(ins::add);
        var address = new int[ins.size()];
        int pc = 0, helper = -1, map = -1, early = -1, jump = -1;

        for (int i = 0; i < ins.size(); i++) {
            address[i] = pc;
            var x = ins.get(i);

            if (x instanceof ReferenceInstruction r) {
                String ref = r.getReference().toString();

                if (ref.equals(
                        NaviVinGuard.HELPER
                                + "->selectContent(Ljava/lang/String;Ljava/lang/Object;)Ljava/lang/Object;")) {
                    helper = i;
                }

                if (ref.equals("Ljava/util/Map;->get(Ljava/lang/Object;)Ljava/lang/Object;")) {
                    map = i;
                }
            }

            if (x.getOpcode() == Opcode.RETURN_OBJECT && early < 0) {
                early = i;
            }

            if (x.getOpcode() == Opcode.GOTO) {
                jump = i;
            }

            pc += x.getCodeUnits();
        }

        check(
                early >= 0 && map > early && jump > early && jump < map && helper > map,
                "OEM paths differ");
        check(
                ins.get(jump) instanceof OffsetInstruction
                        && address[jump] + ((OffsetInstruction) ins.get(jump)).getCodeOffset()
                                == address[helper],
                "null Map skips fallback");
        check(
                ins.get(helper + 1).getOpcode() == Opcode.MOVE_RESULT_OBJECT
                        && ins.get(helper + 2).getOpcode() == Opcode.RETURN_OBJECT,
                "missing-map result skips fallback");
    }

    static void tryCoverage() throws Exception {
        var h1 = new ImmutableExceptionHandler("Ljava/lang/Exception;", 6);
        var h2 = new ImmutableExceptionHandler("Ljava/lang/Error;", 7);
        var handlers = List.<org.jf.dexlib2.iface.ExceptionHandler>of(h1, h2);
        var split = synthetic(new int[][] {{0, 2}, {2, 2}, {4, 2}}, handlers, false);
        var whole = synthetic(new int[][] {{0, 6}}, handlers, false);

        check(
                coverage(split).equals(coverage(whole)),
                "per-instruction exception coverage differs");
        String expected = FrameworkDexMaterializer.semantic(NaviVinGuard.canonical(whole));
        check(
                expected.equals(FrameworkDexMaterializer.semantic(NaviVinGuard.canonical(split))),
                "adjacent identical handlers must preserve coverage");

        for (var bad :
                List.of(
                        synthetic(new int[][] {{0, 2}, {3, 3}}, handlers, false),
                        synthetic(new int[][] {{0, 3}, {2, 4}}, handlers, false),
                        synthetic(new int[][] {{0, 6}}, List.of(h1), false),
                        synthetic(new int[][] {{0, 6}}, List.of(h2, h1), false),
                        synthetic(new int[][] {{0, 6}}, handlers, true))) {
            check(
                    !expected.equals(
                            FrameworkDexMaterializer.semantic(NaviVinGuard.canonical(bad))),
                    "changed protected coverage, handler order or instruction accepted");
        }
    }

    static void check(boolean v, String m) {
        if (!v) {
            throw new AssertionError(m);
        }
    }

    static long refs(Method m) {
        long n = 0;

        for (var i : m.getImplementation().getInstructions()) {
            if (i instanceof ReferenceInstruction r
                    && r.getReference()
                            .toString()
                            .equals(
                                    NaviVinGuard.HELPER
                                            + "->selectContent(Ljava/lang/String;Ljava/lang/Object;)Ljava/lang/Object;")) {
                n++;
            }
        }

        return n;
    }

    public static void main(String[] a) throws Exception {
        tryCoverage();
        Path original = Path.of(a[0]);
        byte[] initial;

        try (var z = new ZipFile(original.toFile())) {
            initial = z.getInputStream(z.getEntry("classes3.dex")).readAllBytes();
        }

        NaviVinGuard.validate(NaviVinGuard.PROFILE, 30, "arm64-v8a", original, initial);
        byte[] version =
                NaviVersionGuard.apply(NaviVinGuard.PROFILE, 30, "arm64-v8a", original, initial);
        NaviVinGuard.validate(NaviVinGuard.PROFILE, 30, "arm64-v8a", original, version);
        byte[] before =
                NaviVehicleGuard.apply(NaviVinGuard.PROFILE, 30, "arm64-v8a", original, version);
        var old = NaviVinGuard.classes(before);
        check(refs(NaviVinGuard.method(old)) == 0, "RED OEM empty input remains empty");
        byte[] after = NaviVinGuard.apply(NaviVinGuard.PROFILE, 30, "arm64-v8a", original, before);
        var out = NaviVinGuard.classes(after);
        check(
                old.keySet().equals(out.keySet()) && refs(NaviVinGuard.method(out)) == 1,
                "one genuine fallback required");
        assertVinPaths(NaviVinGuard.method(out));

        for (var e : old.entrySet()) {
            if (!e.getKey().equals(NaviVinGuard.OWNER)) {
                check(
                        NaviVinGuard.same(e.getValue(), out.get(e.getKey())),
                        "unrelated class drift");
            }
        }

        for (var m : old.get(NaviVinGuard.OWNER).getMethods()) {
            if (!m.getName().equals("getContent")) {
                Method same = null;

                for (var x : out.get(NaviVinGuard.OWNER).getMethods()) {
                    if (x.getName().equals(m.getName())
                            && x.getParameterTypes().equals(m.getParameterTypes())
                            && x.getReturnType().equals(m.getReturnType())) {
                        same = x;
                    }
                }

                check(
                        same != null
                                && FrameworkDexMaterializer.semantic(m)
                                        .equals(FrameworkDexMaterializer.semantic(same)),
                        "unrelated method drift");
            }
        }

        System.out.println(
                "PASS exact OEM VIN setter, genuine fallback and unrelated method preservation");
    }
}
