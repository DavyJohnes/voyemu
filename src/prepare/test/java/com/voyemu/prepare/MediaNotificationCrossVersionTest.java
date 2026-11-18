package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public final class MediaNotificationCrossVersionTest {
    static final String PROFILE = "free-11-chinese-20250710-6.11.1",
            OTHER = "free-11-chinese-20260731-7.3.1";

    interface Check {
        void run() throws Exception;
    }

    static void rejects(Check c) throws Exception {
        try {
            c.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("Notification guard accepted drift");
    }

    static Method method(byte[] dex) {
        Method found = null;

        for (var c : PassionServiceForegroundGuard.classes(dex).values()) {
            for (var m : c.getMethods()) {
                if (MediaNotificationGuard.target(m)) {
                    if (found != null) {
                        throw new AssertionError("duplicate");
                    }

                    found = m;
                }
            }
        }

        if (found == null) {
            throw new AssertionError("method missing");
        }

        return found;
    }

    static int[] compact(Method m, long actions) {
        var code = new ArrayList<Instruction>();

        m.getImplementation().getInstructions().forEach(code::add);
        int at = -1;

        for (int i = 0; i < code.size(); i++) {
            if (code.get(i) instanceof ReferenceInstruction r
                    && r.getReference().toString().equals(MediaNotificationGuard.COMPACT)) {
                at = i;
            }
        }

        if (at < 10) {
            throw new AssertionError("compact bridge missing");
        }

        long masked = 0;
        int length = 0;
        int[] result = null;

        for (int i = at - 10; i < at; i++) {
            switch (i - (at - 10)) {
                case 0 -> {
                    if (code.get(i).getOpcode() != Opcode.INVOKE_VIRTUAL
                            || !((ReferenceInstruction) code.get(i))
                                    .getReference()
                                    .toString()
                                    .equals(
                                            "Landroid/support/v4/media/session/PlaybackStateCompat;->getActions()J")) {
                        throw new AssertionError("actions source");
                    }
                }
                case 1 -> {
                    if (code.get(i).getOpcode() != Opcode.MOVE_RESULT_WIDE
                            || ((OneRegisterInstruction) code.get(i)).getRegisterA() != 2) {
                        throw new AssertionError("actions register");
                    }

                    masked = actions;
                }
                case 2 -> {
                    if (code.get(i).getOpcode() != Opcode.CONST_WIDE_16
                            || ((OneRegisterInstruction) code.get(i)).getRegisterA() != 4
                            || ((NarrowLiteralInstruction) code.get(i)).getNarrowLiteral()
                                    != 0x30) {
                        throw new AssertionError("mask");
                    }
                }
                case 3 -> {
                    if (code.get(i).getOpcode() != Opcode.AND_LONG_2ADDR
                            || ((TwoRegisterInstruction) code.get(i)).getRegisterA() != 2
                            || ((TwoRegisterInstruction) code.get(i)).getRegisterB() != 4) {
                        throw new AssertionError("mask operation");
                    }

                    masked &= 0x30;
                }
                case 4 -> {
                    if (!((ReferenceInstruction) code.get(i))
                            .getReference()
                            .toString()
                            .equals("Ljava/lang/Long;->bitCount(J)I")) {
                        throw new AssertionError("bitcount");
                    }

                    length = Long.bitCount(masked);
                }
                case 5 -> {
                    if (code.get(i).getOpcode() != Opcode.MOVE_RESULT
                            || ((OneRegisterInstruction) code.get(i)).getRegisterA() != 2) {
                        throw new AssertionError("count register");
                    }
                }
                case 6 -> {
                    var ins = (org.jf.dexlib2.iface.instruction.TwoRegisterInstruction) code.get(i);

                    if (code.get(i).getOpcode() != Opcode.ADD_INT_LIT8
                            || ins.getRegisterA() != 2
                            || ins.getRegisterB() != 2
                            || ((NarrowLiteralInstruction) code.get(i)).getNarrowLiteral() != 1) {
                        throw new AssertionError("play count");
                    }

                    length++;
                }
                case 7 -> {
                    if (!((ReferenceInstruction) code.get(i))
                            .getReference()
                            .toString()
                            .equals("Ljava/util/Arrays;->copyOf([II)[I")) {
                        throw new AssertionError("compact copy");
                    }

                    var call = (FiveRegisterInstruction) code.get(i);

                    if (call.getRegisterCount() != 2
                            || call.getRegisterC() != 1
                            || call.getRegisterD() != 2) {
                        throw new AssertionError("compact arguments");
                    }

                    result = Arrays.copyOf(new int[] {0, 1, 2}, length);
                }
                case 8 -> {
                    if (code.get(i).getOpcode() != Opcode.MOVE_RESULT_OBJECT
                            || ((OneRegisterInstruction) code.get(i)).getRegisterA() != 1) {
                        throw new AssertionError("compact result");
                    }
                }
                case 9 -> {
                    if (code.get(i).getOpcode() != Opcode.NOP) {
                        throw new AssertionError("alignment");
                    }
                }
                default -> throw new AssertionError("bridge slot " + i);
            }
        }

        return result;
    }

    static void verifyOriginalActions(Method m) {
        int at = 0, masks = 0, adds = 0;

        for (var ins : m.getImplementation().getInstructions()) {
            if (at == 0x77 || at == 0x93) {
                if (ins.getOpcode() != Opcode.CONST_WIDE_16
                        || ((NarrowLiteralInstruction) ins).getNarrowLiteral()
                                != (at == 0x77 ? 16 : 32)) {
                    throw new AssertionError("OEM previous/next mask differs");
                }

                masks++;
            }

            if (ins instanceof ReferenceInstruction ref
                    && ref.getReference()
                            .toString()
                            .equals(
                                    "Landroidx/core/app/NotificationCompat$Builder;->addAction(Landroidx/core/app/NotificationCompat$Action;)Landroidx/core/app/NotificationCompat$Builder;")) {
                adds++;
            }

            at += ins.getCodeUnits();
        }

        if (masks != 2 || adds != 3) {
            throw new AssertionError("OEM action layout differs");
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        Path apk = root.resolve(".local/profiles/" + PROFILE + "/originals/QGMedia.apk");
        Path foreign = root.resolve(".local/profiles/" + OTHER + "/originals/QGMedia.apk");
        String p = PROFILE, abi = "arm64-v8a";
        byte[] raw = NativeCore.entries(apk).get("classes2.dex");
        byte[] fixed = MediaNotificationGuard.apply(p, 30, abi, apk, raw);

        verifyOriginalActions(method(raw));

        for (int bits = 0; bits < 4; bits++) {
            for (boolean playing : List.of(false, true)) {
                long actions =
                        ((bits & 1) != 0 ? 16 : 0) | ((bits & 2) != 0 ? 32 : 0) | 0x100000000L;
                var actual = new ArrayList<String>();

                if ((actions & 16) != 0) {
                    actual.add("previous");
                }

                actual.add(playing ? "pause" : "play");

                if ((actions & 32) != 0) {
                    actual.add("next");
                }

                int[] indices = compact(method(fixed), actions);

                if (indices.length != actual.size()) {
                    throw new AssertionError("compact count");
                }

                for (int i = 0; i < indices.length; i++) {
                    if (indices[i] != i || indices[i] >= actual.size()) {
                        throw new AssertionError("invalid action index");
                    }
                }
            }
        }

        rejects(() -> MediaNotificationGuard.apply(p, 30, abi, foreign, raw));
        rejects(() -> MediaNotificationGuard.apply(OTHER, 30, abi, apk, raw));
        rejects(() -> MediaNotificationGuard.apply(p, 30, abi, apk, fixed));
    }
}
