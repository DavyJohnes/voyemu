package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;

import java.nio.file.Path;
import java.util.Arrays;
import java.util.zip.ZipFile;

public final class NaviVersionGuardTest {
    static void check(boolean ok, String message) {
        if (!ok) {
            throw new AssertionError(message);
        }
    }

    static boolean writesOnAosp(byte[] dex) {
        var d = new DexBackedDexFile(Opcodes.forApi(30), dex);

        for (var c : d.getClasses()) {
            for (var m : c.getMethods()) {
                if (NaviVersionGuard.target(m)) {
                    int address = 0, result = 0;
                    boolean conditional = false;

                    for (var i : m.getImplementation().getInstructions()) {
                        if (address == 3) {
                            if (i.getOpcode() == org.jf.dexlib2.Opcode.MOVE_RESULT) {
                                result = 0;
                            } else if (i.getOpcode() == org.jf.dexlib2.Opcode.CONST_4) {
                                result = ((NarrowLiteralInstruction) i).getNarrowLiteral();
                            } else {
                                throw new AssertionError("result instruction");
                            }
                        }

                        if (address == 4) {
                            conditional =
                                    i.getOpcode() == org.jf.dexlib2.Opcode.IF_EQZ
                                            && ((OffsetInstruction) i).getCodeOffset() == 3;
                        }

                        if (address == 6) {
                            check(
                                    conditional
                                            && i.getOpcode() == org.jf.dexlib2.Opcode.RETURN_VOID,
                                    "original early return");

                            return result == 0;
                        }

                        address += i.getCodeUnits();
                    }
                }
            }
        }

        throw new AssertionError("method absent");
    }

    interface Checked {
        void run() throws Exception;
    }

    static void reject(Checked f) throws Exception {
        try {
            f.run();
            throw new AssertionError("unexpected acceptance");
        } catch (Failure e) {
            check(e.code.equals("NAVI_VERSION_CONTRACT"), "contract");
        }
    }

    public static void main(String[] args) throws Exception {
        Path apk = Path.of(args[0]);
        byte[] before;

        try (var z = new ZipFile(apk.toFile())) {
            before = z.getInputStream(z.getEntry("classes3.dex")).readAllBytes();
        }

        check(writesOnAosp(before), "RED: original enters rejected System write on AOSP channel");
        byte[] after = NaviVersionGuard.apply(TestProfiles.SYNTHETIC, 30, "arm64-v8a", apk, before);
        check(!writesOnAosp(after), "GREEN: original OEM metadata early return");
        check(Arrays.equals(before, NaviVersionGuard.binary(after, true)), "exact DEX inverse");
        int bodyBytes = 0;

        for (int i = 32; i < before.length; i++) {
            if (before[i] != after[i]) {
                bodyBytes++;
            }
        }

        check(bodyBytes == 2, "only one DEX instruction changed beyond checksums");
        reject(() ->
                NaviVersionGuard.validate(TestProfiles.SYNTHETIC, 28, "arm64-v8a", apk, before));
        reject(() ->
                NaviVersionGuard.validate(TestProfiles.SYNTHETIC, 30, "armeabi-v7a", apk, before));
        reject(() ->
                NaviVersionGuard.validate(TestProfiles.SYNTHETIC, 30, "arm64-v8a", apk, after));
        System.out.println(
                "PASS original AOSP write RED, OEM metadata return GREEN, exact DEX inverse and"
                        + " API/ABI gates");
    }
}
