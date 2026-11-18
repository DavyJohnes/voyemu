package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;

import java.nio.file.Path;

public final class PassionSettingsUnitsGuardTest {
    interface Checked {
        void run() throws Exception;
    }

    static void rejects(Checked c) throws Exception {
        try {
            c.run();
            throw new AssertionError("invalid Units input accepted");
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }
    }

    public static void main(String[] a) throws Exception {
        Path apk = Path.of(a[0]);
        byte[] raw = NativeCore.entries(apk).get("classes5.dex");
        String p = PassionSettingsUnitsGuard.PROFILE;

        PassionSettingsUnitsGuard.validateOriginalChain(p, 30, apk, raw);
        byte[] prior = PassionSettingsSoundGuard.apply(p, 30, apk, raw),
                out = PassionSettingsUnitsGuard.apply(p, 30, apk, prior);
        var dex = new DexBackedDexFile(Opcodes.forApi(30), out);
        int count = 0;

        for (var c : dex.getClasses()) {
            if (c.getType().equals(PassionSettingsUnitsGuard.OWNER)) {
                for (var m : c.getVirtualMethods()) {
                    if (PassionSettingsUnitsGuard.target(m)) {
                        var it = m.getImplementation().getInstructions().iterator();
                        var read = it.next();
                        var branch = it.next();

                        if (read.getOpcode() != Opcode.IGET_OBJECT
                                || branch.getOpcode() != Opcode.IF_EQZ
                                || ((OneRegisterInstruction) branch).getRegisterA() != 0
                                || ((OffsetInstruction) branch).getCodeOffset() != 5) {
                            throw new AssertionError("null must return, nonnull must call OEM");
                        }

                        count++;
                    }
                }
            }
        }

        if (count != 2) {
            throw new AssertionError("both lifecycle methods required");
        }

        rejects(() -> PassionSettingsUnitsGuard.apply("wrong", 30, apk, prior));
        rejects(() -> PassionSettingsUnitsGuard.apply(p, 29, apk, prior));
        rejects(() -> PassionSettingsUnitsGuard.apply(p, 30, apk, out));
        System.out.println(
                "Units null skips connect/disconnect, non-null Car path structurally preserved and"
                        + " full inverse PASS; units service unavailable remains unsupported");
    }
}
