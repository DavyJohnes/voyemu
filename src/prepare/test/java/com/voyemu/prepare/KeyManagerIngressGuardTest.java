package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction35c;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

/**
 * Actual pinned DEX and all-class inverse; own JVM dispatch tests cover authentication separately.
 */
public final class KeyManagerIngressGuardTest {
    static final Map<String, Object> STEP =
            Map.of(
                    StepConstants.PINS,
                    List.of("951abb4f86540663fd654138b7e58cd0226d5dfa0470a4f5e85384bcdedbb78e"));

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

        throw new AssertionError("invalid ingress accepted");
    }

    private static Method selected(byte[] dex) {
        for (var type : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            if (type.getType().equals(KeyManagerIngressGuard.OWNER)) {
                for (var method : type.getMethods()) {
                    if (method.getName().equals("inputQGKeyEvent")) {
                        return method;
                    }
                }
            }
        }

        throw new AssertionError("missing actual Binder method");
    }

    private static int calls(byte[] dex, String owner) {
        int count = 0;

        for (var type : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            for (var method : type.getMethods()) {
                if (method.getImplementation() != null) {
                    for (var instruction : method.getImplementation().getInstructions()) {
                        if (instruction instanceof ReferenceInstruction referenceInstruction
                                && referenceInstruction
                                        .getReference()
                                        .toString()
                                        .startsWith(owner)) {
                            count++;
                        }
                    }
                }
            }
        }

        return count;
    }

    private static Method changed(Method source, int registers, Instruction site, boolean extra) {
        var instructions = new ArrayList<Instruction>();
        int address = 0;

        for (var instruction : source.getImplementation().getInstructions()) {
            instructions.add(address == 45 && site != null ? site : instruction);
            address += instruction.getCodeUnits();
        }

        if (extra) {
            instructions.add(new ImmutableInstruction10x(Opcode.NOP));
        }

        return new ImmutableMethod(
                source.getDefiningClass(),
                source.getName(),
                source.getParameters(),
                source.getReturnType(),
                source.getAccessFlags(),
                source.getAnnotations(),
                source.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(
                        registers,
                        instructions,
                        source.getImplementation().getTryBlocks(),
                        source.getImplementation().getDebugItems()));
    }

    public static void main(String[] args) throws Exception {
        Path owner = Path.of(args[0]), out = Path.of(args[1]);

        Files.createDirectory(out);
        byte[] original;

        try (var zip = new ZipFile(owner.toFile())) {
            original = zip.getInputStream(zip.getEntry("classes.dex")).readAllBytes();
        }

        byte[] predecessor = KeyManagerGuard.apply(STEP, 30, owner, original);
        boolean legacy = args.length == 3 && args[2].equals("--legacy");
        byte[] output =
                legacy ? predecessor : KeyManagerIngressGuard.apply(STEP, 30, owner, predecessor);
        check(
                calls(output, "Lcom/voyemu/input/FreeWheelIngress;->route") == 1,
                "actual original/legacy tx7 has zero ingress bridge (RED)");
        check(
                calls(output, "Lcom/voyemu/input/FreeWheelMediaActions;") == 4
                        && calls(output, "Lcom/voyemu/input/FreeWheelCancellation;") == 1,
                "existing media/cancel unchanged");
        KeyManagerIngressGuard.validateMethod(selected(output), true);
        Method method = selected(predecessor);
        KeyManagerIngressGuard.validateMethod(method, false);
        denied(() -> KeyManagerIngressGuard.validateMethod(changed(method, 6, null, false), false));
        denied(() -> KeyManagerIngressGuard.validateMethod(changed(method, 5, null, true), false));
        denied(() ->
                KeyManagerIngressGuard.validateMethod(
                        changed(
                                method,
                                5,
                                new ImmutableInstruction35c(
                                        Opcode.INVOKE_VIRTUAL,
                                        2,
                                        3,
                                        1,
                                        0,
                                        0,
                                        0,
                                        new ImmutableMethodReference(
                                                "Lcom/qinggan/keymanager/service/engine/KeyManagerEngine;",
                                                "pushMsg",
                                                List.of(
                                                        "Lcom/qinggan/keymanager/service/engine/KeyManagerMessage;"),
                                                "Z")),
                                false),
                        false));
        denied(() ->
                KeyManagerIngressGuard.validateMethod(
                        changed(
                                method,
                                5,
                                new ImmutableInstruction35c(
                                        Opcode.INVOKE_VIRTUAL,
                                        2,
                                        3,
                                        0,
                                        0,
                                        0,
                                        0,
                                        new ImmutableMethodReference(
                                                "Lcom/qinggan/keymanager/service/engine/KeyManagerEngine;",
                                                "other",
                                                List.of(
                                                        "Lcom/qinggan/keymanager/service/engine/KeyManagerMessage;"),
                                                "Z")),
                                false),
                        false));
        denied(() -> KeyManagerIngressGuard.validateMethod(selected(output), false));
        denied(() -> KeyManagerIngressGuard.apply(STEP, 28, owner, predecessor));
        denied(() -> KeyManagerIngressGuard.apply(STEP, 30, owner, output));
        Path bad = out.resolve("foreign.apk");
        Files.writeString(bad, "foreign");
        denied(() -> KeyManagerIngressGuard.apply(STEP, 30, bad, predecessor));
        Files.write(out.resolve("classes.dex"), output);
        System.out.println(
                "Pinned actual original legacy RED -> one tx7 35c ingress GREEN; all-class inverse,"
                    + " original reader/media/cancel preserved;"
                    + " registers/extent/reference/predecessor/reapply/profile/API/owner negatives"
                    + " PASS. No APK activation or guest execution.");
    }
}
