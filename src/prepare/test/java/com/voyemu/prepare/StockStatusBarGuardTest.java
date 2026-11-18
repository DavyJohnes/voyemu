package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.TreeMap;
import java.util.zip.ZipFile;

public final class StockStatusBarGuardTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    static void check(boolean b, String s) {
        if (!b) {
            throw new AssertionError(s);
        }
    }

    static Map<String, ClassDef> classes(byte[] d) {
        var out = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), d).getClasses()) {
            check(out.put(c.getType(), c) == null, "No duplicate classes");
        }

        return out;
    }

    static void verify(byte[] source, byte[] output) throws Exception {
        var before = classes(source);
        var after = classes(output);

        check(before.keySet().equals(after.keySet()), "Same class inventory");
        int replacements = 0;

        for (var entry : before.entrySet()) {
            var current = after.get(entry.getKey());

            if (!entry.getKey().equals(StockStatusBarGuard.OWNER)) {
                check(
                        FrameworkDexMaterializer.semantic(entry.getValue())
                                .equals(FrameworkDexMaterializer.semantic(current)),
                        "Every unrelated class retained");
                continue;
            }

            var methods = new HashMap<String, Method>();

            for (var m : current.getMethods()) {
                methods.put(m.toString(), m);
            }

            for (var m : entry.getValue().getMethods()) {
                var n = methods.remove(m.toString());
                check(n != null, "Same methods");

                if (!StockStatusBarGuard.target(m)) {
                    check(
                            FrameworkDexMaterializer.semantic(m)
                                    .equals(FrameworkDexMaterializer.semantic(n)),
                            "Other stock controller methods retained");
                    continue;
                }

                var a = new ArrayList<Instruction>();
                var b = new ArrayList<Instruction>();
                m.getImplementation().getInstructions().forEach(a::add);
                n.getImplementation().getInstructions().forEach(b::add);
                check(a.size() == b.size(), "No added instructions");
                int address = 0;

                for (int i = 0; i < a.size(); i++) {
                    var x = a.get(i);
                    var y = b.get(i);
                    check(x.getCodeUnits() == y.getCodeUnits(), "Stable branch offsets");

                    if (address == (m.getName().equals("attach") ? 0x43 : 0x18)) {
                        check(
                                x.getOpcode() == Opcode.INVOKE_INTERFACE
                                        && y.getOpcode() == Opcode.INVOKE_STATIC,
                                "Only lifecycle invocation redirected");
                        var xr = (FiveRegisterInstruction) x;
                        var yr = (FiveRegisterInstruction) y;
                        check(
                                xr.getRegisterCount() == yr.getRegisterCount()
                                        && xr.getRegisterC() == yr.getRegisterC()
                                        && xr.getRegisterD() == yr.getRegisterD()
                                        && xr.getRegisterE() == yr.getRegisterE(),
                                "Preserved manager/view/params");
                        check(
                                ((ReferenceInstruction) y)
                                        .getReference()
                                        .toString()
                                        .equals(
                                                StockStatusBarGuard.ROUTER
                                                        + "->"
                                                        + (m.getName().equals("attach")
                                                                ? "addView"
                                                                : "updateViewLayout")
                                                        + "(Landroid/view/WindowManager;Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V"),
                                "Exact own helper");
                        replacements++;
                    } else {
                        check(
                                FrameworkDexMaterializer.semantic(x)
                                        .equals(FrameworkDexMaterializer.semantic(y)),
                                "Every other instruction retained");
                    }

                    address += x.getCodeUnits();
                }
            }

            check(methods.isEmpty(), "No extra methods");
        }

        check(replacements == 2, "Two guarded lifecycle calls");
    }

    interface Checked {
        void run() throws Exception;
    }

    static void reject(Checked c) throws Exception {
        try {
            c.run();
            throw new AssertionError("Invalid boundary accepted");
        } catch (Failure e) {
            check(e.code.equals("STOCK_STATUSBAR_CONTRACT"), "Exact contract failure");
        }
    }

    public static void main(String[] a) throws Exception {
        Path original = Path.of(a[0]);
        byte[] d;

        try (var z = new ZipFile(original.toFile())) {
            d = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }

        if (a.length > 1 && a[1].equals("--original")) {
            verify(d, d);

            return;
        }

        boolean red = false;

        try {
            verify(d, d);
        } catch (AssertionError expected) {
            red = true;
        }

        check(red, "Original must fail desired lifecycle policy");
        byte[] output = StockStatusBarGuard.apply(PROFILE, 30, original, d);
        verify(d, output);
        reject(() -> StockStatusBarGuard.apply("other", 30, original, d));
        reject(() -> StockStatusBarGuard.apply(PROFILE, 28, original, d));
        reject(() -> StockStatusBarGuard.apply(PROFILE, 30, original, null));
        reject(() -> StockStatusBarGuard.apply(PROFILE, 30, original, output));
        reject(() -> StockStatusBarGuard.apply(PROFILE, 30, null, d));
        Path destination = Path.of(a[1]);
        Path wrong = destination.resolveSibling("wrong-original.apk");
        Files.writeString(wrong, "wrong original", StandardOpenOption.CREATE_NEW);

        try {
            reject(() -> StockStatusBarGuard.apply(PROFILE, 30, wrong, d));
        } finally {
            Files.delete(wrong);
        }

        Files.write(destination, output);
        System.out.println(
                "Two stock lifecycle calls redirected; unrelated classes and instructions retained;"
                        + " seven invalid boundaries rejected");
    }
}
