package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ThreeRegisterInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Objects;
import java.util.TreeMap;

public final class SoundBalanceGuardTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    static void check(boolean x, String m) {
        if (!x) {
            throw new AssertionError(m);
        }
    }

    static Method method(byte[] dex) {
        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            if (c.getType().equals(SoundBalanceGuard.OWNER)) {
                for (var m : c.getVirtualMethods()) {
                    if (m.getName().equals(SoundBalanceGuard.METHOD)) {
                        return m;
                    }
                }
            }
        }

        throw new AssertionError();
    }

    // Execute the actual method prefix up to its first side effect, including both array reads.
    static int[] run(Method m, int[] input) {
        var xs = new TreeMap<Integer, Instruction>();
        int pc = 0;

        for (var i : m.getImplementation().getInstructions()) {
            xs.put(pc, i);
            pc += i.getCodeUnits();
        }

        Object[] r = new Object[6];
        pc = 0;

        for (int steps = 0; steps < 30; steps++) {
            var i = xs.get(pc);
            int next = pc + i.getCodeUnits();
            int a = i instanceof OneRegisterInstruction x ? x.getRegisterA() : -1;

            switch (i.getOpcode()) {
                case IGET_OBJECT, INVOKE_VIRTUAL -> {}
                case MOVE_RESULT_OBJECT -> r[a] = input;
                case IF_EQZ -> {
                    if (r[a] == null) {
                        next = pc + ((OffsetInstruction) i).getCodeOffset();
                    }
                }
                case ARRAY_LENGTH -> {
                    var x = (TwoRegisterInstruction) i;
                    r[a] = ((int[]) r[x.getRegisterB()]).length;
                }
                case CONST_4 -> r[a] = ((NarrowLiteralInstruction) i).getNarrowLiteral();
                case IF_LT -> {
                    var x = (TwoRegisterInstruction) i;

                    if ((int) r[a] < (int) r[x.getRegisterB()]) {
                        next = pc + ((OffsetInstruction) i).getCodeOffset();
                    }
                }
                case AGET -> {
                    var x = (ThreeRegisterInstruction) i;
                    r[a] = ((int[]) r[x.getRegisterB()])[(int) r[x.getRegisterC()]];
                }
                case SGET_OBJECT -> {
                    return new int[] {(int) r[0], (int) r[1]};
                }
                case RETURN_VOID -> {
                    return null;
                }
                default -> throw new AssertionError("unexpected " + i.getOpcode());
            }

            pc = next;
        }

        throw new AssertionError("loop");
    }

    static byte[] input(String[] args) throws java.io.IOException {
        if (args.length == 1) {
            return Files.readAllBytes(Path.of(args[0]));
        }

        if (args.length != 2 || !args[0].equals("--apk")) {
            throw new IllegalArgumentException("RAW_DEX or --apk ORIGINAL_APK required");
        }

        try (var zip = new java.util.zip.ZipFile(args[1])) {
            var entry = zip.getEntry("classes.dex");
            check(entry != null && !entry.isDirectory(), "actual classes.dex required");
            check(
                    entry.getSize() >= 112 && entry.getSize() <= 32L * 1024 * 1024,
                    "bounded actual classes.dex required");

            try (var stream = zip.getInputStream(entry)) {
                byte[] bytes = stream.readNBytes(32 * 1024 * 1024 + 1);
                check(
                        bytes.length == entry.getSize() && bytes.length <= 32 * 1024 * 1024,
                        "truncated or oversized classes.dex");

                return bytes;
            }
        }
    }

    public static void main(String[] args) throws Exception {
        byte[] before = input(args);

        System.out.println(
                "input_dex_sha256="
                        + NativeCore.hash(before)
                        + " input_dex_bytes="
                        + before.length);
        boolean red = false;

        try {
            run(method(before), new int[0]);
        } catch (ArrayIndexOutOfBoundsException e) {
            red = true;
        }

        check(red, "original must reproduce crash");
        byte[] after = SoundBalanceGuard.apply(PROFILE, 30, before);
        check(run(method(after), null) == null, "null");
        check(run(method(after), new int[0]) == null, "empty");
        check(run(method(after), new int[] {9}) == null, "short");
        check(Arrays.equals(run(method(after), new int[] {7, 8}), new int[] {7, 8}), "valid");
        check(Arrays.equals(run(method(after), new int[] {7, 8, 9}), new int[] {7, 8}), "long");
        var base = new TreeMap<String, ClassDef>();
        var changed = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), before).getClasses()) {
            base.put(c.getType(), c);
        }

        byte[] canonical =
                FrameworkDexMaterializer.serialize(
                        new org.jf.dexlib2.immutable.ImmutableDexFile(
                                Opcodes.forApi(30), base.values()));
        base.clear();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), canonical).getClasses()) {
            base.put(c.getType(), c);
        }

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), after).getClasses()) {
            changed.put(c.getType(), c);
        }

        check(base.keySet().equals(changed.keySet()), "class inventory");

        for (var e : base.entrySet()) {
            if (!e.getKey().equals(SoundBalanceGuard.OWNER)) {
                check(
                        FrameworkDexMaterializer.semantic(e.getValue())
                                .equals(FrameworkDexMaterializer.semantic(changed.get(e.getKey()))),
                        "unrelated class " + e.getKey());
            }
        }

        var originalMethods = new TreeMap<String, String>();

        for (var m : base.get(SoundBalanceGuard.OWNER).getMethods()) {
            originalMethods.put(m.toString(), FrameworkDexMaterializer.semantic(m));
        }

        for (var m : changed.get(SoundBalanceGuard.OWNER).getMethods()) {
            if (!m.getName().equals(SoundBalanceGuard.METHOD)) {
                check(
                        Objects.equals(
                                originalMethods.get(m.toString()),
                                FrameworkDexMaterializer.semantic(m)),
                        "unrelated method " + m);
            }
        }

        boolean rejected = false;

        try {
            SoundBalanceGuard.apply(PROFILE, 30, after);
        } catch (Failure e) {
            rejected = true;
        }

        check(rejected, "already patched must reject");
        System.out.println(
                "RED empty array; GREEN null/empty/short skipped; valid and long arrays preserve"
                        + " values; drift rejected");
    }
}
