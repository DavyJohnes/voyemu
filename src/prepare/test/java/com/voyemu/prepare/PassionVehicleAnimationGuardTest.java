package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.iface.reference.FieldReference;
import org.jf.dexlib2.iface.reference.MethodReference;
import org.jf.dexlib2.immutable.ImmutableDexFile;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.TreeMap;
import java.util.zip.ZipFile;

public final class PassionVehicleAnimationGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static void check(boolean value) {
        if (!value) {
            throw new AssertionError("Electric animation cleanup");
        }
    }

    static void reject(Action action) throws Exception {
        try {
            action.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("contract accepted");
    }

    static void npe(Action action) throws Exception {
        try {
            action.run();
        } catch (NullPointerException expected) {
            return;
        }

        throw new AssertionError("original null binding did not fail");
    }

    static final class Handle {
        int stops;
    }

    static final class Image {
        int clears;
    }

    static final class Binding {
        Image image = new Image();
    }

    static final class State {
        Handle animation, task;
        Binding binding;
    }

    // Execute only the actual pinned method's opcodes; unsupported instructions fail the test.
    static void run(Method method, State state) {
        var code = new TreeMap<Integer, Instruction>();
        int at = 0;

        for (var i : method.getImplementation().getInstructions()) {
            code.put(at, i);
            at += i.getCodeUnits();
        }

        Object[] r = new Object[3];
        r[2] = state;
        int pc = 0;

        for (int steps = 0; steps < 64; steps++) {
            Instruction i = Objects.requireNonNull(code.get(pc));
            int next = pc + i.getCodeUnits();

            switch (i.getOpcode()) {
                case CONST_4 -> {
                    var x = (OneRegisterInstruction) i;
                    check(((NarrowLiteralInstruction) i).getNarrowLiteral() == 0);
                    r[x.getRegisterA()] = null;
                }
                case IGET_OBJECT -> {
                    var x = (TwoRegisterInstruction) i;
                    Object receiver = Objects.requireNonNull(r[x.getRegisterB()]);
                    String name =
                            ((FieldReference) ((ReferenceInstruction) i).getReference()).getName();
                    r[x.getRegisterA()] =
                            switch (name) {
                                case "mAnimDrawable" -> ((State) receiver).animation;
                                case "mBinding" -> ((State) receiver).binding;
                                case "imgChargingBatter" -> ((Binding) receiver).image;
                                case "mPlayAnimTask" -> ((State) receiver).task;
                                default -> throw new AssertionError(name);
                            };
                }
                case IPUT_OBJECT -> {
                    var x = (TwoRegisterInstruction) i;
                    State receiver = (State) Objects.requireNonNull(r[x.getRegisterB()]);
                    String name =
                            ((FieldReference) ((ReferenceInstruction) i).getReference()).getName();

                    if (name.equals("mAnimDrawable")) {
                        receiver.animation = (Handle) r[x.getRegisterA()];
                    } else if (name.equals("mPlayAnimTask")) {
                        receiver.task = (Handle) r[x.getRegisterA()];
                    } else {
                        throw new AssertionError(name);
                    }
                }
                case CHECK_CAST -> {
                    Object value = r[((OneRegisterInstruction) i).getRegisterA()];
                    check(value == null || value instanceof Binding);
                }
                case IF_EQZ -> {
                    if (r[((OneRegisterInstruction) i).getRegisterA()] == null) {
                        next = pc + ((OffsetInstruction) i).getCodeOffset();
                    }
                }
                case INVOKE_VIRTUAL -> {
                    var x = (FiveRegisterInstruction) i;
                    Object receiver = Objects.requireNonNull(r[x.getRegisterC()]);
                    String name =
                            ((MethodReference) ((ReferenceInstruction) i).getReference()).getName();

                    if (name.equals("stop")) {
                        ((Handle) receiver).stops++;
                    } else if (name.equals("setBackground")) {
                        check(r[x.getRegisterD()] == null);
                        ((Image) receiver).clears++;
                    } else {
                        throw new AssertionError(name);
                    }
                }
                case RETURN_VOID -> {
                    return;
                }
                default -> throw new AssertionError(i.getOpcode());
            }

            pc = next;
        }

        throw new AssertionError("execution bound");
    }

    static State state(boolean binding, boolean animation, boolean task) {
        State s = new State();

        s.binding = binding ? new Binding() : null;
        s.animation = animation ? new Handle() : null;
        s.task = task ? new Handle() : null;

        return s;
    }

    static void cleanup(Method method, boolean binding, boolean animation, boolean task) {
        State s = state(binding, animation, task);
        Handle a = s.animation, t = s.task;
        Binding b = s.binding;

        run(method, s);
        check(s.animation == null && s.task == null);
        check(a == null || a.stops == 1);
        check(t == null || t.stops == 1);
        check(b == null || b.image.clears == 1);
    }

    static Method destroy(Map<String, ClassDef> types) {
        for (var m : types.get(PassionVehicleAnimationGuard.OWNER).getVirtualMethods()) {
            if (m.getName().equals("onDestroy")) {
                return m;
            }
        }

        throw new AssertionError("onDestroy");
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), out = Path.of(args[1]);

        Files.createDirectory(out);
        Path apk =
                root.resolve(
                        ".local/profiles/"
                                + PassionVehicleAnimationGuard.PROFILE
                                + "/originals/LY_Car.apk");
        byte[] raw;

        try (var zip = new ZipFile(apk.toFile())) {
            raw = zip.getInputStream(zip.getEntry("classes3.dex")).readAllBytes();
        }

        byte[] prepared =
                PassionVehicleAnimationGuard.prepareOriginal(
                        PassionVehicleAnimationGuard.PROFILE, 30, apk, raw);

        for (byte[] input : List.of(raw, prepared)) {
            var types = PassionVehicleAnimationGuard.classes(input);
            Method original = PassionVehicleAnimationGuard.method(types);
            PassionVehicleAnimationGuard.validateMethod(original);
            State red = state(false, true, true);
            Handle animation = red.animation, task = red.task;
            npe(() -> run(original, red));
            check(animation.stops == 1 && red.animation == null && task.stops == 0);
            var expected = PassionVehicleAnimationGuard.rewrite(types, false);
            byte[] bytes =
                    FrameworkDexMaterializer.serialize(
                            new ImmutableDexFile(Opcodes.forApi(30), expected.values()));
            var actual = PassionVehicleAnimationGuard.classes(bytes);
            PassionVehicleAnimationGuard.same(expected, actual);
            PassionVehicleAnimationGuard.same(
                    types, PassionVehicleAnimationGuard.rewrite(actual, true));
            Method guarded = PassionVehicleAnimationGuard.method(actual);

            for (boolean b : List.of(false, true)) {
                for (boolean a : List.of(false, true)) {
                    for (boolean t : List.of(false, true)) {
                        cleanup(guarded, b, a, t);
                    }
                }
            }

            State invalidImage = state(true, true, true);
            invalidImage.binding.image = null;
            npe(() -> run(guarded, invalidImage));
            check(
                    FrameworkDexMaterializer.semantic(destroy(types))
                            .equals(FrameworkDexMaterializer.semantic(destroy(actual))));
            int address = 0, returns = 0, guards = 0;

            for (var i : guarded.getImplementation().getInstructions()) {
                if (i.getOpcode() == Opcode.RETURN_VOID) {
                    returns++;
                }

                if (address == 12) {
                    check(
                            i.getOpcode() == Opcode.IF_EQZ
                                    && ((OneRegisterInstruction) i).getRegisterA() == 0
                                    && ((OffsetInstruction) i).getCodeOffset() == 9);
                    guards++;
                }

                address += i.getCodeUnits();
            }

            check(address == 31 && returns == 1 && guards == 1);
            reject(() -> PassionVehicleAnimationGuard.validateMethod(guarded));
        }

        byte[] after =
                PassionVehicleAnimationGuard.applyPrepared(
                        PassionVehicleAnimationGuard.PROFILE, 30, apk, prepared);
        reject(() -> PassionVehicleAnimationGuard.applyPrepared("foreign", 30, apk, prepared));
        reject(() ->
                PassionVehicleAnimationGuard.applyPrepared(
                        PassionVehicleAnimationGuard.PROFILE, 28, apk, prepared));
        reject(() ->
                PassionVehicleAnimationGuard.applyPrepared(
                        PassionVehicleAnimationGuard.PROFILE, 30, apk, null));
        reject(() ->
                PassionVehicleAnimationGuard.applyPrepared(
                        PassionVehicleAnimationGuard.PROFILE, 30, apk, after));
        Path wrong = out.resolve("wrong.apk");
        Files.writeString(wrong, "wrong");
        reject(() ->
                PassionVehicleAnimationGuard.applyPrepared(
                        PassionVehicleAnimationGuard.PROFILE, 30, wrong, prepared));
        System.out.println(
                "Actual raw/prepared null-binding RED; guarded cleanup matrix GREEN; original"
                        + " lifecycle and all-class inverse preserved");
    }
}
