package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.zip.ZipFile;

public class SystemUiRoutingTest {
    static final String OTHER = "free-11-chinese-20260731-7.3.1";

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
        Path apk = Path.of(args[0]), other = Path.of(args[1]);
        byte[] d = dex(apk, "classes.dex");

        SystemUiRouting.validate(PROFILE, 30, apk, d);
        denied(() -> SystemUiRouting.validate(OTHER, 30, apk, d));
        denied(() -> SystemUiRouting.validate(PROFILE, 30, other, d));
        byte[] out = SystemUiRouting.apply(PROFILE, 30, apk, d);

        for (String name : new String[] {"addStatusBarWindow", "addStatusBarWindowSecond"}) {
            var ins = instructions(method(out, SystemUiRouting.OWNER, name));
            check(
                    refs(
                                    ins,
                                    SystemUiRouting.ROUTER
                                            + "->addView(Landroid/view/WindowManager;Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V")
                            == 1,
                    "routed add " + name);
            check(
                    refs(
                                    ins,
                                    SystemUiRouting.ROUTER
                                            + "->removeView(Landroid/view/WindowManager;Landroid/view/View;)V")
                            == 1,
                    "routed remove " + name);
            check(
                    refs(
                                            ins,
                                            "Landroid/view/WindowManager;->addView(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V")
                                    == 0
                            && refs(
                                            ins,
                                            "Landroid/view/WindowManager;->removeView(Landroid/view/View;)V")
                                    == 0,
                    "unrouted call " + name);
        }
    }
}
