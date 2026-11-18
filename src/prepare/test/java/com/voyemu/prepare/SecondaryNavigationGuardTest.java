package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.zip.ZipFile;

/** Actual pinned clean services DEX, not an OEM donor or synthetic policy. */
public final class SecondaryNavigationGuardTest {
    private interface Action {
        void run() throws Exception;
    }

    private static void check(boolean value) {
        if (!value) {
            throw new AssertionError("secondary navigation contract");
        }
    }

    private static void denied(Action action) throws Exception {
        try {
            action.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("foreign policy accepted");
    }

    private static void site(byte[] dex, boolean patched) {
        int found = 0;

        for (var type : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            if (type.getType().equals(SecondaryNavigationGuard.TYPE)) {
                for (var method : type.getDirectMethods()) {
                    if (method.getName().equals("<init>")) {
                        int address = 0;

                        for (var instruction : method.getImplementation().getInstructions()) {
                            if (address == 0x194) {
                                check(
                                        instruction.getOpcode()
                                                == (patched ? Opcode.CONST_4 : Opcode.MOVE_RESULT));

                                if (patched) {
                                    check(
                                            ((NarrowLiteralInstruction) instruction)
                                                            .getNarrowLiteral()
                                                    == 0);
                                }

                                found++;
                            }

                            address += instruction.getCodeUnits();
                        }
                    }
                }
            }
        }

        check(found == 1);
    }

    public static void main(String[] args) throws Exception {
        Path input = Path.of(args[0]), out = Path.of(args[1]);

        Files.createDirectory(out);
        byte[] original;

        try (var jar = new ZipFile(input.toFile())) {
            original = jar.getInputStream(jar.getEntry("classes2.dex")).readAllBytes();
        }

        if (args.length == 3 && args[2].equals("--require-patched")) {
            site(original, true);

            return;
        }

        site(original, false);
        byte[] patched =
                SecondaryNavigationGuard.apply(
                        SecondaryNavigationGuard.PROFILE, 30, input, original);
        site(patched, true);
        denied(() ->
                SecondaryNavigationGuard.apply(
                        "passion-11-russian-20260403-2.1.4.80", 30, input, original));
        denied(() ->
                SecondaryNavigationGuard.apply(
                        "free-09-chinese-20260106-6.0.3", 30, input, original));
        denied(() ->
                SecondaryNavigationGuard.apply(
                        SecondaryNavigationGuard.PROFILE, 28, input, original));
        denied(() ->
                SecondaryNavigationGuard.apply(
                        SecondaryNavigationGuard.PROFILE, 30, input, patched));
        denied(() ->
                SecondaryNavigationGuard.apply(SecondaryNavigationGuard.PROFILE, 30, input, null));
        Path foreign = out.resolve("foreign.jar");
        Files.writeString(foreign, "foreign");
        denied(() ->
                SecondaryNavigationGuard.apply(
                        SecondaryNavigationGuard.PROFILE, 30, foreign, original));
        Files.write(out.resolve("classes2.dex"), patched);
        System.out.println(
                "Exact clean API30 secondary MOVE_RESULT→CONST_4 only; all-class inverse and seven"
                        + " negatives PASS; runtime unverified");
    }
}
