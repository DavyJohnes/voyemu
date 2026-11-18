package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;

import java.nio.file.Path;

public class PassionSettingsBluetoothRoleGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static void reject(Action a) throws Exception {
        boolean yes = false;

        try {
            a.run();
        } catch (Failure x) {
            yes = true;
        }

        if (!yes) {
            throw new AssertionError("expected guard rejection");
        }
    }

    public static void main(String[] a) throws Exception {
        Path p = Path.of(a[0]);
        byte[] d = NativeCore.entries(p).get("classes6.dex");
        var dex = new DexBackedDexFile(Opcodes.forApi(30), d);

        reject(() ->
                PassionBluetoothRoleGuard.validateMethod(PassionBluetoothRoleGuard.method(dex)));
        PassionSettingsBluetoothRoleGuard.validate(
                PassionSettingsBluetoothRoleGuard.PROFILE, 30, p, d);
        byte[] out =
                PassionSettingsBluetoothRoleGuard.apply(
                        PassionSettingsBluetoothRoleGuard.PROFILE, 30, p, d);
        var actual = new DexBackedDexFile(Opcodes.forApi(30), out);
        var m = PassionSettingsBluetoothRoleGuard.method(actual);
        int at = 0;
        boolean constant = false, nop = false;

        for (var i : m.getImplementation().getInstructions()) {
            if (at == 37) {
                constant =
                        i.getOpcode() == Opcode.CONST
                                && ((OneRegisterInstruction) i).getRegisterA() == 1
                                && ((NarrowLiteralInstruction) i).getNarrowLiteral() == 0;
            }

            if (at == 40) {
                nop = i.getOpcode() == Opcode.NOP;
            }

            at += i.getCodeUnits();
        }

        if (!constant || !nop) {
            throw new AssertionError("optional role flag must be false");
        }

        reject(() -> PassionSettingsBluetoothRoleGuard.apply("other", 30, p, d));
        reject(() ->
                PassionSettingsBluetoothRoleGuard.apply(
                        PassionSettingsBluetoothRoleGuard.PROFILE, 29, p, d));
        reject(() ->
                PassionSettingsBluetoothRoleGuard.apply(
                        PassionSettingsBluetoothRoleGuard.PROFILE, 30, p, out));
        System.out.println(
                "CarSettings role OEM RED -> false optional role GREEN, full inverse + four"
                        + " rejection cases PASS");
    }
}
