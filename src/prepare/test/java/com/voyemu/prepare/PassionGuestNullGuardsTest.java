package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.zip.ZipFile;

public class PassionGuestNullGuardsTest {
    interface Action {
        void run() throws Exception;
    }

    static void check(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    static void reject(Action r) throws Exception {
        try {
            r.run();
        } catch (Failure e) {
            return;
        }

        throw new AssertionError("accepted bad contract");
    }

    static byte[] dex(Path apk) throws Exception {
        try (var z = new ZipFile(apk.toFile())) {
            return z.getInputStream(z.getEntry("classes2.dex")).readAllBytes();
        }
    }

    static Method method(byte[] d, PassionGuestNullGuards.Target t) {
        for (var c : PassionServiceForegroundGuard.classes(d).values()) {
            for (var m : c.getMethods()) {
                if (PassionGuestNullGuards.selected(m, t)) {
                    return m;
                }
            }
        }

        throw new AssertionError();
    }

    static class Fixture {
        Object cache, raw, value, service;
        boolean result;
        int rawReads, valueReads, writes, serviceWrites;
    }

    // Executes only the two pinned method instruction vocabularies; this is a behavior fixture, NOT
    // ART verification.
    static Object run(Method m, Fixture f, boolean ecu) {
        TreeMap<Integer, Instruction> code = new TreeMap<>();
        int end = 0;

        for (var i : m.getImplementation().getInstructions()) {
            code.put(end, i);
            end += i.getCodeUnits();
        }

        Object[] r = new Object[m.getImplementation().getRegisterCount()];
        Object pending = null;
        r[ecu ? 1 : 2] = f;
        r[ecu ? 0 : 3] = 0;
        int pc = 0;

        for (int steps = 0; steps < 100; steps++) {
            Instruction i = code.get(pc);

            if (i == null) {
                throw new AssertionError("pc " + pc);
            }

            int next = pc + i.getCodeUnits();
            String ref =
                    i instanceof ReferenceInstruction reference
                            ? reference.getReference().toString()
                            : "";

            switch (i.getOpcode()) {
                case IGET_OBJECT -> r[((TwoRegisterInstruction) i).getRegisterA()] = f.cache;
                case CONST, CONST_4 ->
                        r[((OneRegisterInstruction) i).getRegisterA()] =
                                ((NarrowLiteralInstruction) i).getNarrowLiteral();
                case IF_EQZ, IF_NEZ -> {
                    Object v = r[((OneRegisterInstruction) i).getRegisterA()];
                    boolean zero = v == null || (v instanceof Integer number && number == 0);

                    if (zero == (i.getOpcode() == Opcode.IF_EQZ)) {
                        next = pc + ((OffsetInstruction) i).getCodeOffset();
                    }
                }
                case INVOKE_STATIC, INVOKE_VIRTUAL, INVOKE_INTERFACE ->
                        pending = invoke(ref, (FiveRegisterInstruction) i, r, f);
                case MOVE_RESULT, MOVE_RESULT_OBJECT ->
                        r[((OneRegisterInstruction) i).getRegisterA()] = pending;
                case IPUT_OBJECT -> {
                    f.cache = r[((TwoRegisterInstruction) i).getRegisterA()];
                    f.writes++;
                }
                case RETURN_OBJECT -> {
                    Object value = r[((OneRegisterInstruction) i).getRegisterA()];

                    return value instanceof Integer number && number == 0 ? null : value;
                }
                case RETURN -> {
                    return r[((OneRegisterInstruction) i).getRegisterA()];
                }
                default -> throw new AssertionError(i.getOpcode());
            }

            pc = next;
        }

        throw new AssertionError("loop");
    }

    static Object invoke(String ref, FiveRegisterInstruction a, Object[] r, Fixture f) {
        Object receiver = a.getRegisterCount() > 0 ? r[a.getRegisterC()] : null;
        Object pending;

        if (ref.contains("TextUtils;->isEmpty")) {
            pending = receiver == null || receiver.toString().isEmpty() ? 1 : 0;
        } else if (ref.contains("->getRawProperty")) {
            f.rawReads++;
            pending = f.raw;
        } else if (ref.contains("CarPropertyValue;->getValue")) {
            if (receiver == null) {
                throw new NullPointerException();
            }

            f.valueReads++;
            pending = f.value;
        } else if (ref.contains("Object;->toString")) {
            if (receiver == null) {
                throw new NullPointerException();
            }

            pending = receiver.toString();
        } else if (ref.contains("->getNexusService")) {
            pending = f.service;
        } else if (ref.contains("IMegaNexusManager;->setFlyingScreenMode")) {
            if (receiver == null) {
                throw new NullPointerException();
            }

            f.serviceWrites++;
            pending = f.result ? 1 : 0;
        } else {
            throw new AssertionError(ref);
        }

        return pending;
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), out = Path.of(args[1]);

        Files.createDirectories(out);
        int negatives = 0;
        Map<String, Object> receipt = new LinkedHashMap<>();

        for (var t : List.of(PassionGuestNullGuards.ECU, PassionGuestNullGuards.AVM)) {
            Path original =
                    root.resolve(
                            ".local/profiles/"
                                    + TestProfiles.pinning(root, PassionGuestNullGuards.ECU.apk())
                                    + "/originals/"
                                    + t.role()
                                    + ".apk");
            byte[] raw = dex(original);
            byte[] changed =
                    PassionGuestNullGuards.applyPrepared(
                            TestProfiles.SYNTHETIC, 30, original, raw, t);
            Method before = method(raw, t), after = method(changed, t);
            PassionGuestNullGuards.validateAfter(before, after, t);
            var mutated = new MutableMethodImplementation(after.getImplementation());
            int branchIndex =
                    PassionServiceForegroundGuard.index(
                            mutated, t.equals(PassionGuestNullGuards.ECU) ? 15 : 4);
            var branch = mutated.getInstructions().get(branchIndex);
            mutated.replaceInstruction(
                    branchIndex,
                    new BuilderInstruction21t(
                            Opcode.IF_NEZ,
                            t.equals(PassionGuestNullGuards.ECU) ? 1 : 3,
                            ((org.jf.dexlib2.builder.instruction.BuilderInstruction21t) branch)
                                    .getTarget()));
            var wrong = PassionServiceForegroundGuard.copy(after, mutated);
            reject(() -> PassionGuestNullGuards.validateAfter(before, wrong, t));
            negatives++;
            check(
                    FrameworkDexMaterializer.semantic(
                                    PassionGuestNullGuards.rewrite(after, t, true))
                            .equals(FrameworkDexMaterializer.semantic(before)));
            boolean ecu = t.equals(PassionGuestNullGuards.ECU);

            if (ecu) {
                checkTypedReturns(after);
            }

            checkBehavior(before, after, ecu);
            negatives += rejectInverse(original, raw, t);
            receipt.put(
                    t.role(),
                    Map.of(
                            "raw_dex",
                            NativeCore.hash(raw),
                            "output_dex",
                            NativeCore.hash(changed),
                            "method_semantic",
                            FrameworkDexMaterializer.semantic(before),
                            "all_class_inverse",
                            true));
            System.out.println(
                    t.role()
                            + " RED null crash -> GREEN truthful unavailable, supported paths"
                            + " preserved");
        }

        receipt.put("negative_checks", negatives);
        receipt.put("ART_verified", false);
        receipt.put("runtime_verified", false);
        receipt.put("canonical_changed", false);
        Files.writeString(out.resolve("null-guards-receipt.json"), Json.write(receipt) + "\n");
        System.out.println("PASS " + negatives + " negatives; original+actualpreparedCar inverse");
    }

    static void checkTypedReturns(Method after) {
        var typed = new MutableMethodImplementation(after.getImplementation());

        for (int address : new int[] {15, 23}) {
            int at = PassionServiceForegroundGuard.index(typed, address);
            var zero = typed.getInstructions().get(at + 1);
            check(
                    zero.getOpcode() == Opcode.CONST_4
                            && ((OneRegisterInstruction) zero).getRegisterA() == 0
                            && ((NarrowLiteralInstruction) zero).getNarrowLiteral() == 0
                            && typed.getInstructions().get(at + 2).getOpcode()
                                    == Opcode.RETURN_OBJECT);
        }
    }

    static void checkBehavior(Method before, Method after, boolean ecu) {
        Fixture absent = new Fixture();

        try {
            run(before, absent, ecu);
            throw new AssertionError("RED missing");
        } catch (NullPointerException expected) {
            // The unguarded original is expected to fail here.
        }

        Fixture missing = new Fixture();
        Object missingResult = run(after, missing, ecu);
        check(ecu ? missingResult == null : missingResult.equals(0));
        check(missing.writes == 0 && missing.serviceWrites == 0);

        if (ecu) {
            Fixture valnull = new Fixture();
            valnull.raw = new Object();
            check(
                    run(after, valnull, true) == null
                            && valnull.writes == 0
                            && valnull.valueReads == 1);
            Fixture supported = new Fixture();
            supported.raw = new Object();
            supported.value = "real-HW-7";
            check(run(after, supported, true).equals("real-HW-7") && supported.writes == 1);
            Fixture cached = new Fixture();
            cached.cache = "cached-real-HW";
            check(run(after, cached, true).equals("cached-real-HW") && cached.rawReads == 0);
        } else {
            for (boolean success : new boolean[] {false, true}) {
                Fixture supported = new Fixture();
                supported.service = new Object();
                supported.result = success;
                check(
                        run(after, supported, false).equals(success ? 1 : 0)
                                && supported.serviceWrites == 1);
            }
        }
    }

    static int rejectInverse(Path original, byte[] raw, PassionGuestNullGuards.Target t)
            throws Exception {
        int negatives = 0;
        boolean ecu = t.equals(PassionGuestNullGuards.ECU);

        reject(() ->
                PassionGuestNullGuards.applyPrepared(TestProfiles.SYNTHETIC, 29, original, raw, t));
        negatives++;

        if (ecu) {
            byte[] prior =
                    PassionVehicleBluetoothGuard.apply(TestProfiles.SYNTHETIC, 30, original, raw);
            prior = PassionAmbientLightGuard.apply(TestProfiles.SYNTHETIC, 30, original, prior);
            PassionGuestNullGuards.applyPrepared(TestProfiles.SYNTHETIC, 30, original, prior, t);
        }

        return negatives;
    }
}
