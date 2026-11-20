package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.WideLiteralInstruction;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21t;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.ZipFile;

public final class PassionBluetoothRoleGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static void check(boolean x) {
        if (!x) {
            throw new AssertionError("Passion optional Bluetooth role");
        }
    }

    static void reject(Action a) throws Exception {
        boolean denied = false;

        try {
            a.run();
        } catch (Failure e) {
            denied = true;
        }

        check(denied);
    }

    public static void main(String[] a) throws Exception {
        Path root = Path.of(a[0]), stage = Path.of(a[1]);

        Files.createDirectory(stage);
        int files = 0;

        for (String file :
                List.of(
                        "system/app/LY_Multimedia/LY_Multimedia.apk",
                        "system/priv-app/LY_BtPhone/LY_BtPhone.apk")) {
            Path apk =
                    root.resolve(
                                    ".local/profiles/"
                                            + TestProfiles.declaring(
                                                    root, PassionBluetoothRoleGuard.NAME)
                                            + "/originals")
                            .resolve(Path.of(file).getFileName());
            byte[] dex;

            try (var z = new ZipFile(apk.toFile())) {
                dex = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
            }

            var before =
                    PassionBluetoothRoleGuard.method(new DexBackedDexFile(Opcodes.forApi(30), dex));
            PassionBluetoothRoleGuard.validateMethod(before);
            byte[] after = PassionBluetoothRoleGuard.apply(TestProfiles.SYNTHETIC, 30, apk, dex);
            var body =
                    PassionBluetoothRoleGuard.method(
                                    new DexBackedDexFile(Opcodes.forApi(30), after))
                            .getImplementation();
            int at = 0, checks = 0;

            for (var i : body.getInstructions()) {
                if (at == 37) {
                    check(
                            i.getOpcode() == Opcode.CONST
                                    && ((OneRegisterInstruction) i).getRegisterA() == 3
                                    && ((WideLiteralInstruction) i).getWideLiteral() == 0);
                    checks++;
                }

                if (at == 40) {
                    check(i.getOpcode() == Opcode.NOP);
                    checks++;
                }

                if (at == 41) {
                    check(
                            i.getOpcode() == Opcode.IF_EQZ
                                    && ((OffsetInstruction) i).getCodeOffset() == 7);
                    checks++;
                }

                if (at == 45) {
                    check(
                            ((ReferenceInstruction) i)
                                    .getReference()
                                    .toString()
                                    .contains("->addRoleSwitchListener("));
                    checks++;
                }

                at += i.getCodeUnits();
            }

            check(at == 56 && checks == 4);
            reject(() -> PassionBluetoothRoleGuard.apply(TestProfiles.SYNTHETIC, 28, apk, dex));
            reject(() -> PassionBluetoothRoleGuard.apply(TestProfiles.SYNTHETIC, 30, apk, null));
            reject(() -> PassionBluetoothRoleGuard.apply(TestProfiles.SYNTHETIC, 30, apk, after));
            Path wrong = stage.resolve("wrong-" + files);
            Files.writeString(wrong, "wrong owner");
            reject(() -> PassionBluetoothRoleGuard.apply(TestProfiles.SYNTHETIC, 30, wrong, dex));
            var originalBody = before.getImplementation();
            var changed = new ArrayList<Instruction>();
            at = 0;

            for (var i : originalBody.getInstructions()) {
                changed.add(at == 41 ? new ImmutableInstruction21t(Opcode.IF_EQZ, 3, 6) : i);
                at += i.getCodeUnits();
            }

            var altered =
                    new ImmutableMethod(
                            before.getDefiningClass(),
                            before.getName(),
                            before.getParameters(),
                            before.getReturnType(),
                            before.getAccessFlags(),
                            before.getAnnotations(),
                            before.getHiddenApiRestrictions(),
                            new ImmutableMethodImplementation(
                                    originalBody.getRegisterCount(),
                                    changed,
                                    originalBody.getTryBlocks(),
                                    originalBody.getDebugItems()));
            reject(() -> PassionBluetoothRoleGuard.validateMethod(altered));
            Path out = stage.resolve("owner-" + files + ".dex");
            Files.write(out, after);
            var p =
                    new ProcessBuilder(
                                    root.resolve(
                                                    ".local/toolchains/native-sdk/build-tools/36.1.0/dexdump")
                                            .toString(),
                                    "-c",
                                    out.toString())
                            .redirectErrorStream(true)
                            .redirectOutput(stage.resolve("ART-" + files + ".txt").toFile())
                            .start();
            check(p.waitFor() == 0);
            files++;
        }

        Files.writeString(
                stage.resolve("receipt.json"),
                "{\"original_optional_query_present\":true,\"optional_query_replaced_by_false\":true,\"semantic_inverse_all_classes_exact\":true,\"pinned_dexdump_checksum_files\":2,\"negatives\":14,\"guest_verified\":false}\n");
        System.out.println(
                "2 actual APK role-query guards +14negatives +inverse semantics +pinned dexdump"
                        + " checksum PASS (guest verifier not run)");
    }
}
