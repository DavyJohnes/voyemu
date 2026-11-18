package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.zip.ZipFile;

public final class PassionVehicleAudioGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static void check(boolean x) {
        if (!x) {
            throw new AssertionError("Passion nullable automotive audio");
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
                                + PassionVehicleAudioGuard.PROFILE
                                + "/originals/LY_Car.apk");
        byte[] dex;

        try (var z = new ZipFile(apk.toFile())) {
            dex = z.getInputStream(z.getEntry("classes3.dex")).readAllBytes();
        }

        PassionVehicleAudioGuard.validate(PassionVehicleAudioGuard.PROFILE, 30, apk, dex);
        var originalInstrument =
                PassionVehicleInstrumentGuard.method(new DexBackedDexFile(Opcodes.forApi(30), dex));
        PassionVehicleInstrumentGuard.validateScratch(originalInstrument, 3);
        reject(() -> PassionVehicleInstrumentGuard.validateScratch(originalInstrument, 1));
        byte[] after =
                PassionVehicleAudioGuard.apply(PassionVehicleAudioGuard.PROFILE, 30, apk, dex);
        var method =
                PassionVehicleAudioGuard.method(new DexBackedDexFile(Opcodes.forApi(30), after));
        checkGuardedRegistration(method);
        var guardedDex = new DexBackedDexFile(Opcodes.forApi(30), after);

        for (String owner :
                List.of(PassionVehicleAudioGuard.OBSERVER, PassionVehicleAudioGuard.SOUND)) {
            checkNullable(guardedDex, owner);
        }

        checkInstrumentRange(guardedDex);
        reject(() -> PassionVehicleInstrumentGuard.validate(guardedDex));

        for (int card : new int[] {-1, Integer.MIN_VALUE, 0, 1, 31, 63, 64, Integer.MAX_VALUE}) {
            boolean unsupported = card < 0 || card >= 64;
            check(unsupported == (Integer.toBinaryString(card).length() > 6));
        }

        rejectWrongInputs(apk, out, dex, after);
        reject(() -> PassionVehicleAudioGuard.validateMethod(method));
        Path file = out.resolve("classes3.dex");
        Files.write(file, after);
        var process =
                new ProcessBuilder(
                                root.resolve(
                                                ".local/toolchains/native-sdk/build-tools/36.1.0/dexdump")
                                        .toString(),
                                "-c",
                                file.toString())
                        .redirectErrorStream(true)
                        .redirectOutput(out.resolve("checksum.txt").toFile())
                        .start();
        check(process.waitFor() == 0);
        System.out.println(
                "Actual nullable CarAudioManager callback: null returns, non-null original"
                    + " cast/store retained; three nullable paths plus unavailable instrument-card"
                    + " range, all-class inverse, ten negatives and DEX checksum PASS");
    }

    static void checkGuardedRegistration(org.jf.dexlib2.iface.Method method) {
        int at = 0, checked = 0;

        for (var i : method.getImplementation().getInstructions()) {
            if (at == 24) {
                check(
                        i.getOpcode() == Opcode.IF_EQZ
                                && ((OneRegisterInstruction) i).getRegisterA() == 1
                                && at + ((OffsetInstruction) i).getCodeOffset() == 61);
                checked++;
            }

            if (at == 26) {
                check(i.getOpcode() == Opcode.NOP);
                checked++;
            }

            if (at == 27) {
                check(
                        i.getOpcode() == Opcode.CHECK_CAST
                                && ((OneRegisterInstruction) i).getRegisterA() == 1);
                checked++;
            }

            if (at == 29) {
                check(
                        i.getOpcode() == Opcode.SPUT_OBJECT
                                && ((OneRegisterInstruction) i).getRegisterA() == 1);
                checked++;
            }

            if (at == 61) {
                check(i.getOpcode() == Opcode.RETURN_VOID);
                checked++;
            }

            at += i.getCodeUnits();
        }

        check(at == 62 && checked == 5);
    }

    static void checkNullable(DexBackedDexFile guardedDex, String owner) throws Exception {
        var guarded = PassionVehicleAudioGuard.nullableMethod(guardedDex, owner);
        boolean observer = owner.equals(PassionVehicleAudioGuard.OBSERVER);
        int address = 0, checks = 0;

        for (var i : guarded.getImplementation().getInstructions()) {
            if (address == (observer ? 17 : 54)) {
                check(
                        i.getOpcode() == Opcode.IF_EQZ
                                && ((OneRegisterInstruction) i).getRegisterA() == (observer ? 2 : 0)
                                && address + ((OffsetInstruction) i).getCodeOffset()
                                        == (observer ? 30 : 59));
                checks++;
            }

            if (address == (observer ? 19 : 56)) {
                check(
                        i.getOpcode() == Opcode.INVOKE_VIRTUAL
                                && ((ReferenceInstruction) i)
                                        .getReference()
                                        .toString()
                                        .contains(
                                                observer
                                                        ? "->registerListener()V"
                                                        : "->setOneKeyMute(Z)I"));
                checks++;
            }

            if (observer && address == 30) {
                check(i.getOpcode() == Opcode.RETURN_VOID);
                checks++;
            }

            if (!observer && address == 59) {
                check(
                        i.getOpcode() == Opcode.IGET_OBJECT
                                && ((ReferenceInstruction) i)
                                        .getReference()
                                        .toString()
                                        .contains("->mMegaCarAudioManager:"));
                checks++;
            }

            address += i.getCodeUnits();
        }

        check(checks == 3 && address == (observer ? 31 : 82));
        reject(() -> PassionVehicleAudioGuard.validateNullable(guarded, observer));
    }

    static void checkInstrumentRange(DexBackedDexFile guardedDex) throws Exception {
        var instrument = PassionVehicleInstrumentGuard.method(guardedDex);
        int ip = 0, ic = 0;

        for (var i : instrument.getImplementation().getInstructions()) {
            ic += checkRangeSite(ip, i) + checkLogSite(ip, i);
            ip += i.getCodeUnits();
        }

        check(ip == 194 && ic == 7);
    }

    static int checkRangeSite(int ip, Instruction i) {
        if (ip == 43) {
            check(
                    i.getOpcode() == Opcode.IF_LTZ
                            && ((OneRegisterInstruction) i).getRegisterA() == 7
                            && ip + ((OffsetInstruction) i).getCodeOffset() == 177);

            return 1;
        }

        if (ip == 45) {
            check(
                    i.getOpcode() == Opcode.CONST_16
                            && ((OneRegisterInstruction) i).getRegisterA() == 3
                            && ((NarrowLiteralInstruction) i).getNarrowLiteral() == 64);

            return 1;
        }

        if (ip == 47) {
            check(
                    i.getOpcode() == Opcode.IF_GE
                            && ((TwoRegisterInstruction) i).getRegisterA() == 7
                            && ((TwoRegisterInstruction) i).getRegisterB() == 3
                            && ip + ((OffsetInstruction) i).getCodeOffset() == 177);

            return 1;
        }

        return 0;
    }

    static int checkLogSite(int ip, Instruction i) {
        if (ip == 49) {
            check(
                    i.getOpcode() == Opcode.INVOKE_STATIC
                            && ((ReferenceInstruction) i)
                                    .getReference()
                                    .toString()
                                    .contains("->toBinaryString(I)"));

            return 1;
        }

        if (ip == 61) {
            check(
                    i.getOpcode() == Opcode.MOVE_RESULT
                            && ((OneRegisterInstruction) i).getRegisterA() == 3);

            return 1;
        }

        if (ip == 104) {
            check(
                    i.getOpcode() == Opcode.INVOKE_STATIC
                            && ((ReferenceInstruction) i)
                                    .getReference()
                                    .toString()
                                    .equals(
                                            "Lcom/voyah/common/log/LogUtils;->d(Ljava/lang/String;Ljava/lang/String;)V")
                            && ((FiveRegisterInstruction) i).getRegisterC() == 1
                            && ((FiveRegisterInstruction) i).getRegisterD() == 7);

            return 1;
        }

        if (ip == 177) {
            check(i.getOpcode() == Opcode.RETURN_VOID);

            return 1;
        }

        return 0;
    }

    static void rejectWrongInputs(Path apk, Path out, byte[] dex, byte[] after) throws Exception {
        reject(() -> PassionVehicleAudioGuard.apply("foreign", 30, apk, dex));
        reject(() ->
                PassionVehicleAudioGuard.apply(PassionVehicleAudioGuard.PROFILE, 28, apk, dex));
        reject(() ->
                PassionVehicleAudioGuard.apply(PassionVehicleAudioGuard.PROFILE, 30, apk, null));
        reject(() ->
                PassionVehicleAudioGuard.apply(PassionVehicleAudioGuard.PROFILE, 30, apk, after));
        Path wrong = out.resolve("wrong.apk");
        Files.writeString(wrong, "wrong");
        reject(() ->
                PassionVehicleAudioGuard.apply(PassionVehicleAudioGuard.PROFILE, 30, wrong, dex));
    }
}
