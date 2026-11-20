package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.WideLiteralInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.zip.ZipFile;

public final class PassionVehicleBluetoothGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static void check(boolean x) {
        if (!x) {
            throw new AssertionError("Passion vehicle optional Bluetooth listener");
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
        Path root = Path.of(a[0]), out = Path.of(a[1]);

        Files.createDirectory(out);
        Path apk =
                root.resolve(
                        ".local/profiles/"
                                + TestProfiles.declaring(root, PassionVehicleBluetoothGuard.NAME)
                                + "/originals/LY_Car.apk");
        byte[] dex;

        try (var z = new ZipFile(apk.toFile())) {
            dex = z.getInputStream(z.getEntry("classes2.dex")).readAllBytes();
        }

        var before = new DexBackedDexFile(Opcodes.forApi(30), dex);
        check(PassionVehicleBluetoothGuard.methods(before).size() == 4);
        byte[] after = PassionVehicleBluetoothGuard.apply(TestProfiles.SYNTHETIC, 30, apk, dex);
        var modified = new DexBackedDexFile(Opcodes.forApi(30), after);
        int methods = 0;

        for (var c : modified.getClasses()) {
            if (c.getType().equals(PassionVehicleBluetoothGuard.OWNER)) {
                for (var m : c.getMethods()) {
                    if (PassionVehicleBluetoothGuard.METHODS.contains(m.getName())) {
                        int at = 0;

                        for (var i : m.getImplementation().getInstructions()) {
                            if (m.getName().equals("getCurrentAvRole")
                                    || m.getName().equals("switchAvRole")) {
                                check(
                                        at == 0
                                                ? i.getOpcode() == Opcode.CONST
                                                        && ((OneRegisterInstruction) i)
                                                                        .getRegisterA()
                                                                == 0
                                                        && ((WideLiteralInstruction) i)
                                                                        .getWideLiteral()
                                                                == (m.getName()
                                                                                .equals(
                                                                                        "getCurrentAvRole")
                                                                        ? -1
                                                                        : 0)
                                                : at == 3
                                                        ? i.getOpcode() == Opcode.NOP
                                                        : i.getOpcode() == Opcode.RETURN);
                            } else {
                                check(
                                        at < 3
                                                ? i.getOpcode() == Opcode.NOP
                                                : i.getOpcode() == Opcode.RETURN_VOID);
                            }

                            at += i.getCodeUnits();
                        }

                        check(
                                at
                                        == ((m.getName().equals("getCurrentAvRole")
                                                        || m.getName().equals("switchAvRole"))
                                                ? 5
                                                : 4));
                        methods++;
                    }
                }
            }
        }

        check(methods == 4);
        reject(() -> PassionVehicleBluetoothGuard.apply(TestProfiles.SYNTHETIC, 28, apk, dex));
        reject(() -> PassionVehicleBluetoothGuard.apply(TestProfiles.SYNTHETIC, 30, apk, null));
        reject(() -> PassionVehicleBluetoothGuard.apply(TestProfiles.SYNTHETIC, 30, apk, after));
        Path wrong = out.resolve("wrong.apk");
        Files.writeString(wrong, "wrong owner");
        reject(() -> PassionVehicleBluetoothGuard.apply(TestProfiles.SYNTHETIC, 30, wrong, dex));
        reject(() -> PassionVehicleBluetoothGuard.methods(modified));
        byte[] settings;

        try (var z = new ZipFile(apk.toFile())) {
            settings = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }

        PassionVehicleBluetoothGuard.validateSettings(TestProfiles.SYNTHETIC, 30, apk, settings);
        byte[] guardedSettings =
                PassionVehicleBluetoothGuard.applySettings(
                        TestProfiles.SYNTHETIC, 30, apk, settings);
        var settingsMethod =
                PassionBluetoothRoleGuard.method(
                        new DexBackedDexFile(Opcodes.forApi(30), guardedSettings));
        int address = 0, checked = 0;

        for (var i : settingsMethod.getImplementation().getInstructions()) {
            if (address == 37) {
                check(
                        i.getOpcode() == Opcode.CONST
                                && ((WideLiteralInstruction) i).getWideLiteral() == 0);
                checked++;
            }

            if (address == 40) {
                check(i.getOpcode() == Opcode.NOP);
                checked++;
            }

            address += i.getCodeUnits();
        }

        check(checked == 2);
        reject(() ->
                PassionVehicleBluetoothGuard.applySettings(
                        TestProfiles.SYNTHETIC, 28, apk, settings));
        reject(() ->
                PassionVehicleBluetoothGuard.applySettings(
                        TestProfiles.SYNTHETIC, 30, apk, guardedSettings));
        reject(() ->
                PassionVehicleBluetoothGuard.applySettings(TestProfiles.SYNTHETIC, 30, apk, null));
        Files.write(out.resolve("classes.dex"), guardedSettings);
        Path output = out.resolve("classes2.dex");
        Files.write(output, after);
        var process =
                new ProcessBuilder(
                                root.resolve(
                                                ".local/toolchains/native-sdk/build-tools/36.1.0/dexdump")
                                        .toString(),
                                "-c",
                                output.toString())
                        .redirectErrorStream(true)
                        .redirectOutput(out.resolve("ART-checksum.txt").toFile())
                        .start();
        check(process.waitFor() == 0);
        System.out.println(
                "Actual OEM listener registration reproducer, exact four-wrapper and Settingslib"
                        + " inverse semantics, eleven refusal guards and ART checksum PASS");
    }
}
