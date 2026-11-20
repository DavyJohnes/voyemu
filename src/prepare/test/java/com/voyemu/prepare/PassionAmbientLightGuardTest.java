package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

public class PassionAmbientLightGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static void check(boolean x) {
        if (!x) {
            throw new AssertionError();
        }
    }

    static void reject(Action a) throws Exception {
        try {
            a.run();
        } catch (Failure f) {
            return;
        }

        throw new AssertionError("accepted changed contract");
    }

    public static void main(String[] a) throws Exception {
        Path root = Path.of(a[0]), out = Path.of(a[1]);

        Files.createDirectory(out);
        Path apk =
                root.resolve(
                        ".local/profiles/"
                                + TestProfiles.declaring(root, PassionAmbientLightGuard.NAME)
                                + "/originals/LY_Car.apk");
        int total = 0;

        for (String dex : List.of("classes2.dex", "classes3.dex")) {
            byte[] raw;

            try (var z = new ZipFile(apk.toFile())) {
                raw = z.getInputStream(z.getEntry(dex)).readAllBytes();
            }

            byte[] ear;

            if (dex.equals("classes2.dex")) {
                ear = PassionVehicleBluetoothGuard.apply(TestProfiles.SYNTHETIC, 30, apk, raw);
            } else {
                byte[] audio = PassionVehicleAudioGuard.apply(TestProfiles.SYNTHETIC, 30, apk, raw);
                ear = PassionBluetoothVolumeGuard.apply(TestProfiles.SYNTHETIC, 30, apk, audio);
            }

            String original = NativeCore.hash(raw);
            PassionAmbientLightGuard.validateOriginalChain(
                    TestProfiles.SYNTHETIC, 30, apk, dex, raw);
            check(original.equals(NativeCore.hash(raw)));
            byte[] patched = PassionAmbientLightGuard.apply(TestProfiles.SYNTHETIC, 30, apk, ear);
            var before = PassionAmbientLightGuard.classes(ear);
            var after = PassionAmbientLightGuard.classes(patched);
            int count = 0;

            for (var c : before.values()) {
                for (var m : c.getMethods()) {
                    var t = PassionAmbientLightGuard.target(m);

                    if (t == null) {
                        continue;
                    }

                    var actual =
                            java.util.stream.StreamSupport.stream(
                                            after.get(c.getType()).getMethods().spliterator(),
                                            false)
                                    .filter(n ->
                                            n.getName().equals(m.getName())
                                                    && n.getParameterTypes()
                                                            .equals(m.getParameterTypes()))
                                    .findFirst()
                                    .orElseThrow();
                    PassionAmbientLightGuard.validateAfter(m, actual);

                    if (t.kind() <= 1) {
                        checkManagerBranch(m, actual, t.kind());
                    }

                    if (t.kind() >= 3) {
                        checkOwnerRegisters(actual, t.kind());
                    }

                    var bad = new MutableMethodImplementation(actual.getImplementation());
                    bad.replaceInstruction(0, new BuilderInstruction10x(Opcode.NOP));
                    reject(() ->
                            PassionAmbientLightGuard.validateAfter(
                                    m, PassionServiceForegroundGuard.copy(actual, bad)));
                    count++;
                }
            }

            check(count == (dex.equals("classes2.dex") ? 8 : 3));
            total += count;
            reject(() -> PassionAmbientLightGuard.apply(TestProfiles.SYNTHETIC, 28, apk, ear));
            Files.write(out.resolve(dex), patched);
        }

        check(total == 11);
        Files.writeString(
                out.resolve("receipt.json"),
                Json.write(
                                Map.of(
                                        "methods",
                                        11,
                                        "negative_contracts",
                                        19,
                                        "all_class_inverse",
                                        true,
                                        "raw_preflight_readonly",
                                        true,
                                        "runtime_verified",
                                        false))
                        + "\n");
        System.out.println("Ambient11methods/19negative/inverse PASS");
    }

    static void checkManagerBranch(
            org.jf.dexlib2.iface.Method m, org.jf.dexlib2.iface.Method actual, int kind) {
        var body = new MutableMethodImplementation(actual.getImplementation());
        int at = PassionAmbientLightGuard.managerEnd(actual),
                ix = PassionServiceForegroundGuard.index(body, at);
        var branch = body.getInstructions().get(ix);

        check(branch.getOpcode() == Opcode.IF_NEZ);
        int supported = at + ((OffsetInstruction) branch).getCodeOffset();
        check(
                body.getInstructions()
                                .get(PassionServiceForegroundGuard.index(body, supported))
                                .getOpcode()
                        == Opcode.CHECK_CAST);

        if (kind == 0) {
            var sentinel = body.getInstructions().get(ix + 1);
            check(
                    sentinel.getOpcode() == Opcode.CONST_4
                            && ((NarrowLiteralInstruction) sentinel).getNarrowLiteral() == -1);
        } else if (m.getReturnType().equals("V")) {
            check(body.getInstructions().get(ix + 1).getOpcode() == Opcode.RETURN_VOID);
        } else {
            var zero = body.getInstructions().get(ix + 1);
            check(
                    zero.getOpcode() == Opcode.CONST_4
                            && ((NarrowLiteralInstruction) zero).getNarrowLiteral() == 0
                            && body.getInstructions().get(ix + 2).getOpcode() == Opcode.RETURN);
        }
    }

    static void checkOwnerRegisters(org.jf.dexlib2.iface.Method actual, int kind) {
        var body = new MutableMethodImplementation(actual.getImplementation());
        int at = kind == 3 ? 108 : 0, ix = PassionServiceForegroundGuard.index(body, at);
        int owner = kind == 3 ? 7 : 2;

        for (int j = 0; j < 8; j++) {
            var in = body.getInstructions().get(ix + j);

            if (in instanceof OneRegisterInstruction reg
                    && in.getOpcode() != Opcode.INVOKE_VIRTUAL
                    && in.getOpcode() != Opcode.INVOKE_STATIC) {
                check(reg.getRegisterA() != owner && (kind != 3 || reg.getRegisterA() != 1));
            }
        }

        check(body.getInstructions().get(ix + 8).getOpcode() == Opcode.IF_LTZ);
    }
}
