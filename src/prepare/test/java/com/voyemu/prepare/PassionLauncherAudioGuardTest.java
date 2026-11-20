package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.zip.ZipFile;

public class PassionLauncherAudioGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static void check(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    static void reject(Action a) throws Exception {
        try {
            a.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("unexpected acceptance");
    }

    // Moves the first helper call to a wrong register so the inverse validator must reject it.
    private static MutableMethodImplementation shiftHelperRegister(
            org.jf.dexlib2.iface.Method actual, int registers) {
        var body = new MutableMethodImplementation(actual.getImplementation());

        for (int i = 0; i < body.getInstructions().size(); i++) {
            var instruction = body.getInstructions().get(i);

            if (instruction instanceof ReferenceInstruction r
                    && r.getReference()
                            .toString()
                            .startsWith(PassionLauncherAudioGuard.HELPER + "->")) {
                var regs = (FiveRegisterInstruction) instruction;
                body.replaceInstruction(
                        i,
                        new BuilderInstruction35c(
                                Opcode.INVOKE_STATIC,
                                2,
                                (regs.getRegisterC() + 1) % registers,
                                regs.getRegisterD(),
                                0,
                                0,
                                0,
                                r.getReference()));

                return body;
            }
        }

        throw new AssertionError("helper call missing");
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), stage = Path.of(args[1]);

        Files.createDirectory(stage);
        Path original =
                root.resolve(
                        ".local/profiles/"
                                + TestProfiles.declaring(root, PassionLauncherAudioGuard.NAME)
                                + "/originals/LY_Launcher.apk");
        byte[] raw;

        try (var z = new ZipFile(original.toFile())) {
            raw = z.getInputStream(z.getEntry("classes3.dex")).readAllBytes();
        }

        byte[] sidebar = PassionSidebarGuard.apply(TestProfiles.SYNTHETIC, 30, original, raw),
                ear =
                        PassionBluetoothVolumeGuard.apply(
                                TestProfiles.SYNTHETIC, 30, original, sidebar);
        String rawHash = NativeCore.hash(raw);
        PassionLauncherAudioGuard.validateOriginalChain(TestProfiles.SYNTHETIC, 30, original, raw);
        check(rawHash.equals(NativeCore.hash(raw)));
        byte[] output = PassionLauncherAudioGuard.apply(TestProfiles.SYNTHETIC, 30, original, ear);
        var before = PassionLauncherAudioGuard.classes(ear);
        var after = PassionLauncherAudioGuard.classes(output);
        int methods = 0, negatives = 0;

        for (var c : before.values()) {
            for (var m : c.getMethods()) {
                var t = PassionLauncherAudioGuard.target(m);

                if (t == null) {
                    continue;
                }

                methods++;
                org.jf.dexlib2.iface.Method actual = null;

                for (var next : after.get(c.getType()).getMethods()) {
                    if (next.getName().equals(m.getName())) {
                        actual = next;
                    }
                }

                check(actual != null);
                PassionLauncherAudioGuard.validateAfter(m, actual);
                var bad =
                        PassionServiceForegroundGuard.copy(
                                actual, shiftHelperRegister(actual, t.registers()));
                reject(() -> PassionLauncherAudioGuard.validateAfter(m, bad));
                negatives++;

                if (t.kind() == 0) {
                    var branch =
                            new MutableMethodImplementation(actual.getImplementation())
                                    .getInstructions()
                                    .get(
                                            PassionServiceForegroundGuard.index(
                                                    new MutableMethodImplementation(
                                                            actual.getImplementation()),
                                                    32));
                    check(
                            branch.getOpcode() == Opcode.IF_EQZ
                                    && ((OneRegisterInstruction) branch).getRegisterA() == 2
                                    && ((OffsetInstruction) branch).getCodeOffset() == 38);
                }

                if (t.kind() == 1) {
                    var instructions =
                            new MutableMethodImplementation(m.getImplementation())
                                    .getInstructions();
                    check(
                            instructions.get(0).getOpcode() == Opcode.INVOKE_VIRTUAL
                                    && ((FiveRegisterInstruction) instructions.get(0))
                                                    .getRegisterC()
                                            == 9
                                    && instructions.get(1).getOpcode() == Opcode.MOVE_RESULT_OBJECT
                                    && ((OneRegisterInstruction) instructions.get(1)).getRegisterA()
                                            == 0);
                }
            }
        }

        reject(() -> PassionLauncherAudioGuard.apply(TestProfiles.SYNTHETIC, 28, original, ear));
        negatives++;
        check(methods == 3);
        Files.write(stage.resolve("LY_Launcher.dex"), output);
        Files.writeString(
                stage.resolve("receipt.json"),
                Json.write(
                                Map.of(
                                        "guarded_methods",
                                        methods,
                                        "negative_checks",
                                        negatives,
                                        "all_class_inverse",
                                        true,
                                        "raw_preflight_read_only",
                                        true,
                                        "prepared_ear_chain_pin",
                                        true,
                                        "null_branch_skips_all_mute_state_writes",
                                        true,
                                        "SDK_media_operations_unchanged",
                                        true,
                                        "ART_verified",
                                        false,
                                        "real_guest",
                                        false))
                        + "\n");
        System.out.println(
                "Launcher3method nullable/UI guard +"
                        + negatives
                        + " negatives/inverse PASS; ART/runtime pending");
    }
}
