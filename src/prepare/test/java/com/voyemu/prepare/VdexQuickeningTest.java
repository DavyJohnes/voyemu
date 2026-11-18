package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;

import java.util.Arrays;
import java.util.List;

public final class VdexQuickeningTest {
    static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    static void reject(Runnable work) {
        try {
            work.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("accepted invalid input");
    }

    static void put32(byte[] b, int at, int v) {
        for (int i = 0; i < 4; i++) {
            b[at + i] = (byte) (v >>> (i * 8));
        }
    }

    public static void main(String[] args) throws Exception {
        byte[] table = new byte[48];

        put32(table, 0, 10);
        put32(table, 4, 12);
        table[8] = 0;
        table[9] = 5; // indices0 and2 present
        table[10] = 0;
        table[11] = 7;
        put32(table, 20, 0);
        var layout = new VdexQuickening.Layout(0, table.length, List.of());
        var section = new VdexQuickening.Section(0, 0, 16, 0, 0);
        check(VdexQuickening.offset(table, layout, section, 0) == 10);
        check(VdexQuickening.offset(table, layout, section, 1) == 0);
        check(VdexQuickening.offset(table, layout, section, 2) == 17);
        table[10] = 7;
        table[11] = (byte) 249;
        table[12] = (byte) 255;
        table[13] = (byte) 255;
        table[14] = (byte) 255;
        table[15] = 15;
        check(VdexQuickening.offset(table, layout, section, 0) == 17);
        check(VdexQuickening.offset(table, layout, section, 2) == 10);
        reject(() -> VdexQuickening.offset(table, layout, section, 16));
        reject(() ->
                VdexQuickening.offset(
                        Arrays.copyOf(table, 21),
                        new VdexQuickening.Layout(0, 21, List.of()),
                        section,
                        0));
        reject(() -> new VdexQuickening.Leb(new byte[] {(byte) 128}, 0, 1).read());
        reject(() ->
                new VdexQuickening.Leb(
                                new byte[] {(byte) 128, (byte) 128, (byte) 128, (byte) 128, 16},
                                0,
                                5)
                        .read());
        check(
                new VdexQuickening.Leb(
                                        new byte[] {
                                            (byte) 255, (byte) 255, (byte) 255, (byte) 255, 15
                                        },
                                        0,
                                        5)
                                .read()
                        == -1);
        byte[] vdex = new byte[176];
        System.arraycopy(
                new byte[] {'v', 'd', 'e', 'x', '0', '1', '9', 0, '0', '0', '2', 0},
                0,
                vdex,
                0,
                12);
        put32(vdex, 12, 1);
        put32(vdex, 24, 140);
        System.arraycopy(new byte[] {'c', 'd', 'e', 'x', '0', '0', '1', 0}, 0, vdex, 40, 8);
        put32(vdex, 72, 136);
        put32(vdex, 148, 136);
        check(VdexQuickening.layout(vdex).sections().size() == 1);
        byte[] badVersion = vdex.clone();
        badVersion[6] = '8';
        reject(() -> VdexQuickening.layout(badVersion));
        reject(() -> VdexQuickening.layout(Arrays.copyOf(vdex, 175)));
        byte[] badShared = vdex.clone();
        put32(badShared, 148, 135);
        reject(() -> VdexQuickening.layout(badShared));
        reject(() -> VdexQuickening.layout(new byte[36]));
        reject(() -> VdexQuickening.normal(Opcode.EXECUTE_INLINE));
        check(VdexQuickening.normal(Opcode.IGET_CHAR_QUICK) == Opcode.IGET_CHAR);
        check(VdexQuickening.normal(Opcode.IPUT_QUICK) == Opcode.IPUT);
        check(
                VdexQuickening.normal(Opcode.INVOKE_VIRTUAL_QUICK_RANGE)
                        == Opcode.INVOKE_VIRTUAL_RANGE);
        boolean pinRejected = false;

        try {
            VdexQuickening.pin(new byte[0], "0".repeat(64));
        } catch (Failure expected) {
            pinRejected = true;
        }

        check(pinRejected);
        System.out.println(
                "VDEX metadata bounds, compact offsets, uint32 LEB, opcodes and caller pin tests"
                        + " passed");
    }
}
