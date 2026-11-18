package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.zip.ZipFile;

public class StockStatusBarRouteTest {
    static final String PROFILE = "free-11-chinese-20250710-6.11.1";

    interface Checked {
        void run() throws Exception;
    }

    static void denied(Checked c) throws Exception {
        try {
            c.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("invalid preimage accepted");
    }

    static byte[] dex(Path apk, String name) throws Exception {
        try (var z = new ZipFile(apk.toFile())) {
            return z.getInputStream(z.getEntry(name)).readAllBytes();
        }
    }

    static org.jf.dexlib2.iface.Method method(byte[] dex, String owner, String name) {
        for (var c :
                new org.jf.dexlib2.dexbacked.DexBackedDexFile(
                                org.jf.dexlib2.Opcodes.forApi(30), dex)
                        .getClasses()) {
            if (c.getType().equals(owner)) {
                for (var m : c.getMethods()) {
                    if (m.getName().equals(name) && m.getImplementation() != null) {
                        return m;
                    }
                }
            }
        }

        throw new AssertionError("method missing: " + owner + " " + name);
    }

    static java.util.List<org.jf.dexlib2.iface.instruction.Instruction> instructions(
            org.jf.dexlib2.iface.Method m) {
        var out = new java.util.ArrayList<org.jf.dexlib2.iface.instruction.Instruction>();

        m.getImplementation().getInstructions().forEach(out::add);

        return out;
    }

    static long refs(java.util.List<org.jf.dexlib2.iface.instruction.Instruction> ins, String ref) {
        return ins.stream()
                .filter(i ->
                        i
                                        instanceof
                                        org.jf.dexlib2.iface.instruction.ReferenceInstruction
                                                referenceInstruction
                                && referenceInstruction.getReference().toString().equals(ref))
                .count();
    }

    static void check(boolean b, String why) {
        if (!b) {
            throw new AssertionError(why);
        }
    }

    public static void main(String[] args) throws Exception {
        Path apk = Path.of(args[0]);
        byte[] d = dex(apk, "classes.dex");

        StockStatusBarGuard.validate(PROFILE, 30, apk, d);
        Path foreign = Files.createTempFile("foreign", ".apk");
        Files.writeString(foreign, "foreign");
        denied(() -> StockStatusBarGuard.validate(PROFILE, 30, foreign, d));
        Files.delete(foreign);
        denied(() -> StockStatusBarGuard.validate(PROFILE, 29, apk, d));
        byte[] out = StockStatusBarGuard.apply(PROFILE, 30, apk, d);

        for (String name : new String[] {"attach", "apply"}) {
            var ins = instructions(method(out, StockStatusBarGuard.OWNER, name));
            String action = name.equals("attach") ? "addView" : "updateViewLayout";
            check(
                    refs(
                                    ins,
                                    StockStatusBarGuard.ROUTER
                                            + "->"
                                            + action
                                            + "(Landroid/view/WindowManager;Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V")
                            == 1,
                    "stock route " + name);
            check(
                    refs(
                                    ins,
                                    "Landroid/view/WindowManager;->"
                                            + action
                                            + "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V")
                            == 0,
                    "stock original invocation " + name);
        }
    }
}
