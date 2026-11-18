package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21s;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction31i;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.zip.ZipFile;

public final class PassionSidebarGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static void check(boolean x) {
        if (!x) {
            throw new AssertionError("actual Passion sidebar");
        }
    }

    static void reject(Action x) throws Exception {
        boolean denied = false;

        try {
            x.run();
        } catch (Failure e) {
            denied = true;
        }

        check(denied);
    }

    static Method altered(Method m, int address, int register, int value) {
        var body = m.getImplementation();
        var list = new ArrayList<Instruction>();
        int at = 0;

        for (var i : body.getInstructions()) {
            list.add(
                    at == address
                            ? new ImmutableInstruction21s(Opcode.CONST_16, register, value)
                            : i);
            at += i.getCodeUnits();
        }

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(
                        body.getRegisterCount(), list, body.getTryBlocks(), body.getDebugItems()));
    }

    static Method extraLiteral(Method m) {
        var body = m.getImplementation();
        var list = new ArrayList<Instruction>();
        int at = 0;

        for (var i : body.getInstructions()) {
            list.add(at == 41 ? new ImmutableInstruction31i(Opcode.CONST, 7, 2019) : i);
            at += i.getCodeUnits();
        }

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(
                        body.getRegisterCount(), list, body.getTryBlocks(), body.getDebugItems()));
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), stage = Path.of(args[1]);

        Files.createDirectory(stage);
        Path original =
                root.resolve(
                        ".local/profiles/"
                                + PassionSidebarGuard.PROFILE
                                + "/originals/LY_Launcher.apk");
        byte[] dex;

        try (ZipFile z = new ZipFile(original.toFile())) {
            dex = z.getInputStream(z.getEntry("classes3.dex")).readAllBytes();
        }

        Method before = PassionSidebarGuard.method(new DexBackedDexFile(Opcodes.forApi(30), dex));
        PassionSidebarGuard.validateMethod(before);
        byte[] output = PassionSidebarGuard.apply(PassionSidebarGuard.PROFILE, 30, original, dex);
        Method after = PassionSidebarGuard.method(new DexBackedDexFile(Opcodes.forApi(30), output));
        int address = 0, count = 0, configured = 0;

        for (var i : after.getImplementation().getInstructions()) {
            if (i instanceof ReferenceInstruction ref
                    && ref.getReference()
                            .toString()
                            .equals(
                                    PassionSidebarGuard.ROUTER
                                            + "->configure(Landroid/view/WindowManager$LayoutParams;)V")) {
                check(
                        (address == 88 || address == 175)
                                && i.getOpcode() == Opcode.INVOKE_STATIC
                                && ((FiveRegisterInstruction) i).getRegisterCount() == 1
                                && ((FiveRegisterInstruction) i).getRegisterC() == 0);
                configured++;
                continue;
            }

            if (address == 39 || address == 128) {
                check(((NarrowLiteralInstruction) i).getNarrowLiteral() == 2038);
                count++;
            }

            address += i.getCodeUnits();
        }

        check(count == 2 && configured == 2 && address == 176);
        reject(() ->
                PassionSidebarGuard.apply("free-11-chinese-20260731-7.3.1", 30, original, dex));
        reject(() -> PassionSidebarGuard.apply(PassionSidebarGuard.PROFILE, 28, original, dex));
        reject(() -> PassionSidebarGuard.apply(PassionSidebarGuard.PROFILE, 30, original, null));
        reject(() -> PassionSidebarGuard.apply(PassionSidebarGuard.PROFILE, 30, original, output));
        Path wrong = stage.resolve("wrong.apk");
        Files.writeString(wrong, "not the owner");
        reject(() -> PassionSidebarGuard.apply(PassionSidebarGuard.PROFILE, 30, wrong, dex));
        reject(() -> PassionSidebarGuard.validateMethod(altered(before, 39, 6, 2018)));
        reject(() -> PassionSidebarGuard.validateMethod(altered(before, 39, 7, 2019)));
        reject(() -> PassionSidebarGuard.validateMethod(altered(before, 128, 7, 2018)));
        reject(() -> PassionSidebarGuard.validateMethod(extraLiteral(before)));
        check(
                FrameworkDexMaterializer.semantic(before.getImplementation().getDebugItems())
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        after.getImplementation().getDebugItems())));
        Path produced = stage.resolve("classes3.dex");
        Files.write(produced, output);
        Process p =
                new ProcessBuilder(
                                root.resolve(
                                                ".local/toolchains/native-sdk/build-tools/36.1.0/dexdump")
                                        .toString(),
                                "-c",
                                produced.toString())
                        .redirectErrorStream(true)
                        .redirectOutput(stage.resolve("ART-dexdump.txt").toFile())
                        .start();
        check(p.waitFor() == 0);
        Files.writeString(
                stage.resolve("receipt.json"),
                "{\"actual_original_reserved_window_type_RED\":true,\"exact_two_app_overlay_constants_GREEN\":true,\"all_other_class_method_semantics_preserved\":true,\"negative_cases\":10,\"ART_exit\":0,\"guest_verified\":false}\n");
        System.out.println(
                "actual two-sidebar constants and exact2 configured params, preserved OEM"
                        + " semantics/debug,10negatives,ART PASS");
    }
}
