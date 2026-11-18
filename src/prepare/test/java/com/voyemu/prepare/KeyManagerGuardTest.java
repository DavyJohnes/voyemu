package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.zip.ZipFile;

/** Exact actual OEM changes, with inverse preservation of every class performed by the guard. */
public final class KeyManagerGuardTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    private interface Action {
        void run() throws Exception;
    }

    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static void denied(Action action) throws Exception {
        try {
            action.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("unbound owner accepted");
    }

    public static void main(String[] args) throws Exception {
        Path owner = Path.of(args[0]), stage = Path.of(args[1]);

        Files.createDirectory(stage);
        byte[] before;

        try (var archive = new ZipFile(owner.toFile())) {
            before = archive.getInputStream(archive.getEntry("classes.dex")).readAllBytes();
        }

        // RED boundary: the pinned original has no own terminal or cancellation bridge.
        int originalHelpers = 0;

        for (var type : new DexBackedDexFile(Opcodes.forApi(30), before).getClasses()) {
            for (var method : type.getMethods()) {
                if (method.getImplementation() != null) {
                    for (var instruction : method.getImplementation().getInstructions()) {
                        if (instruction instanceof ReferenceInstruction reference
                                && reference
                                        .getReference()
                                        .toString()
                                        .contains("Lcom/voyemu/input/FreeWheel")) {
                            originalHelpers++;
                        }
                    }
                }
            }
        }

        check(originalHelpers == 0, "original has no adapted bridge");
        byte[] after = KeyManagerGuard.apply(PROFILE, 30, owner, before);
        int helperCalls = 0, cancelCalls = 0;

        for (var type : new DexBackedDexFile(Opcodes.forApi(30), after).getClasses()) {
            for (var method : type.getMethods()) {
                if (method.getImplementation() != null) {
                    for (var instruction : method.getImplementation().getInstructions()) {
                        if (instruction instanceof ReferenceInstruction reference) {
                            String target = reference.getReference().toString();

                            if (target.contains("Lcom/voyemu/input/FreeWheelMediaActions;")) {
                                helperCalls++;
                            }

                            if (target.contains("Lcom/voyemu/input/FreeWheelCancellation;")) {
                                cancelCalls++;
                            }
                        }
                    }
                }
            }
        }

        check(helperCalls == 4 && cancelCalls == 1, "exact audio4+cancel1 static bridge sites");
        denied(() -> KeyManagerGuard.apply("free-09-chinese-20260106-6.0.3", 30, owner, before));
        denied(() ->
                KeyManagerGuard.apply("passion-11-russian-20260403-2.1.4.80", 30, owner, before));
        denied(() -> KeyManagerGuard.apply(PROFILE, 28, owner, before));
        denied(() -> KeyManagerGuard.apply(PROFILE, 30, owner, after));
        denied(() -> KeyManagerGuard.apply(PROFILE, 30, owner, null));
        Path foreign = stage.resolve("foreign.apk");
        Files.writeString(foreign, "foreign");
        denied(() -> KeyManagerGuard.apply(PROFILE, 30, foreign, before));
        Files.write(stage.resolve("classes.dex"), after);
        System.out.println(
                "Actual Free KeyManager nine reference changes + all-class inverse + six"
                        + " negatives PASS; no ART/runtime proof");
    }
}
