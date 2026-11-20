package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

/**
 * The original ThirdScreen of both Free 11 profiles starts its home and registers its touch
 * listener through RearScreen.
 */
public final class ThirdScreenRoutingTest {
    interface Action {
        void run() throws Exception;
    }

    static void check(boolean b, String text) {
        if (!b) {
            throw new AssertionError(text);
        }
    }

    static void reject(Action a) throws Exception {
        try {
            a.run();
            throw new AssertionError("Wrong ThirdScreen boundary accepted");
        } catch (Failure e) {
            check(e.code.equals("THIRDSCREEN_ROUTING"), "Exact contract failure");
        }
    }

    static byte[] dex(Path apk) throws Exception {
        try (var zip = new ZipFile(apk.toFile())) {
            return zip.getInputStream(zip.getEntry(ThirdScreenRouting.DEX)).readAllBytes();
        }
    }

    static Method method(byte[] dex, String name) {
        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            for (var m : c.getMethods()) {
                if (m.toString().equals(name)) {
                    return m;
                }
            }
        }

        throw new AssertionError("Missing " + name);
    }

    static Instruction site(Method m, int at) {
        int address = 0;

        for (var instruction : m.getImplementation().getInstructions()) {
            if (address == at) {
                return instruction;
            }

            address += instruction.getCodeUnits();
        }

        throw new AssertionError("Missing call in " + m);
    }

    static void routed(byte[] dex, String name, int at, String callee, List<Integer> registers) {
        var call = site(method(dex, name), at);

        check(
                call.getOpcode() == Opcode.INVOKE_STATIC
                        && ((ReferenceInstruction) call).getReference().toString().equals(callee)
                        && ThirdScreenRouting.arguments((FiveRegisterInstruction) call)
                                .equals(registers),
                name + " calls " + callee + " with " + registers);
    }

    static Map<String, Object> admitting(Path apk) throws Exception {
        return Map.of(StepConstants.PINS, List.of(FilesSupport.hash(apk)));
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 2) {
            throw new IllegalArgumentException("611_APK 731_APK");
        }

        Path q611 = Path.of(args[0]), q731 = Path.of(args[1]);

        for (Path apk : List.of(q611, q731)) {
            Map<String, Object> constants = admitting(apk);
            byte[] original = dex(apk);
            ThirdScreenRouting.validate(constants, 30, apk, original);
            byte[] routed = ThirdScreenRouting.apply(constants, 30, apk, original);
            // The start passes this, the intent and the options to RearScreen.
            routed(
                    routed,
                    "Lcom/qinggan/app/thirdscreen/ThirdScreenApplication;->startActivity()V",
                    0x1c,
                    "Lcom/voyemu/output/RearScreen;->startActivity("
                            + "Landroid/content/Context;Landroid/content/Intent;"
                            + "Landroid/os/Bundle;)V",
                    List.of(3, 1, 0));
            // The lock countdown passes its touch listener and display 3 to RearScreen.
            routed(
                    routed,
                    "Lcom/qinggan/app/vehicle/Lock3ScreenManager;-><init>()V",
                    0x2c,
                    "Lcom/voyemu/output/RearScreen;->registerTouchListener("
                            + "Landroid/app/IOnTouchListener;I)V",
                    List.of(1, 2));
            // The original calls the OEM IWindowManager registration that the API30 base lacks.
            var originalCall =
                    site(
                            method(
                                    original,
                                    "Lcom/qinggan/app/vehicle/Lock3ScreenManager;-><init>()V"),
                            0x2c);
            check(
                    ((ReferenceInstruction) originalCall)
                            .getReference()
                            .toString()
                            .equals(
                                    "Lcom/qinggan/os/ServiceManager;->registerIOnTouchListener("
                                            + "Landroid/app/IOnTouchListener;I)V"),
                    "the original lock countdown registers through the OEM ServiceManager");
            // A routed or foreign input and a step without its pin or another API are rejected.
            reject(() -> ThirdScreenRouting.apply(constants, 30, apk, routed));
            reject(() -> ThirdScreenRouting.validate(constants, 28, apk, original));
            reject(() ->
                    ThirdScreenRouting.validate(
                            Map.of(StepConstants.PINS, List.of()), 30, apk, original));
        }

        reject(() -> ThirdScreenRouting.validate(admitting(q611), 30, q731, dex(q731)));
        System.out.println(
                "ThirdScreen rear display start and touch routing passed for both profiles");
    }
}
