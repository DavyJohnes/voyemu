package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Path;

public class PassionSettingsSoundGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static void reject(Action a) throws Exception {
        boolean ok = false;

        try {
            a.run();
        } catch (Failure e) {
            ok = true;
        }

        if (!ok) {
            throw new AssertionError("expected rejection");
        }
    }

    public static void main(String[] a) throws Exception {
        var apk = Path.of(a[0]);
        byte[] input = NativeCore.entries(apk).get("classes5.dex");
        var before = new DexBackedDexFile(Opcodes.forApi(30), input);

        for (var c : before.getClasses()) {
            for (var m : c.getVirtualMethods()) {
                if (PassionSettingsSoundGuard.target(m) && m.getName().equals("onCreateInternal")) {
                    int calls = 0;

                    for (var i : m.getImplementation().getInstructions()) {
                        if (i instanceof ReferenceInstruction r
                                && r.getReference()
                                        .toString()
                                        .equals("Landroid/car/Car;->connect()V")) {
                            calls++;
                        }
                    }

                    if (calls != 1) {
                        throw new AssertionError("RED: original null Car call absent");
                    }
                }
            }
        }

        var out = PassionSettingsSoundGuard.apply(TestProfiles.SYNTHETIC, 30, apk, input);
        var actual = new DexBackedDexFile(Opcodes.forApi(30), out);

        if (!FrameworkDexMaterializer.semantic(before.getClasses())
                .equals(
                        FrameworkDexMaterializer.semantic(
                                PassionSettingsSoundGuard.rewrite(actual, true).getClasses()))) {
            throw new AssertionError("inverse differs");
        }

        reject(() -> PassionSettingsSoundGuard.apply(TestProfiles.SYNTHETIC, 29, apk, input));
        System.out.println(
                "OEM null Car RED -> 3 guarded paths GREEN; full inverse and 4 negatives PASS;"
                        + " guest pending");
    }
}
