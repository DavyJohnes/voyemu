package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class LauncherCanbusRecomputedTest {
    static final String PROFILE = "free-11-chinese-20250710-6.11.1",
            OTHER = "free-11-chinese-20260731-7.3.1";

    static void check(boolean v, String s) {
        if (!v) {
            throw new AssertionError(s);
        }
    }

    static List<String> refs(Method m) {
        var r = new ArrayList<String>();

        for (var i : m.getImplementation().getInstructions()) {
            if (i instanceof ReferenceInstruction x) {
                r.add(x.getReference().toString());
            }
        }

        return r;
    }

    public static void main(String[] a) throws Exception {
        Path original = Path.of(a[0]);
        byte[] raw = LauncherCanbusGuard.original(PROFILE, original);
        byte[] power = LauncherPowerGuard.apply(PROFILE, 30, original, raw, "classes2.dex");
        byte[] aqy = AqyProviderGuard.apply(PROFILE, 30, original, power);
        byte[] before = LauncherDvrGuard.apply(PROFILE, 30, "arm64-v8a", original, aqy);
        byte[] after = LauncherCanbusGuard.apply(PROFILE, 30, "arm64-v8a", original, before);
        var old = LauncherCanbusGuard.classes(before);
        var out = LauncherCanbusGuard.classes(after);
        var register = refs(LauncherCanbusGuard.method(out, LauncherCanbusGuard.REGISTER));

        check(
                !register.contains(LauncherCanbusGuard.GET),
                "RED: startup must not call the connected-only getter before callback");
        var callback = LauncherCanbusGuard.method(out, LauncherCanbusGuard.CALLBACK);
        var calls = refs(callback);
        check(
                Collections.frequency(calls, LauncherCanbusGuard.GET) == 1
                        && calls.stream().anyMatch(x -> x.contains("->onCompassData(FFDDF)V")),
                "real initial snapshot must be retained in connected callback");
        var ins = new ArrayList<Instruction>();
        callback.getImplementation().getInstructions().forEach(ins::add);
        check(
                ins.get(1).getOpcode() == Opcode.IF_EQZ && ins.get(2).getOpcode() == Opcode.IF_EQZ,
                "false/missing-manager callbacks must not read CAN");
        checkUnrelatedPreserved(old, out);
        boolean denied = false;

        try {
            LauncherCanbusGuard.validate(OTHER, 30, "arm64-v8a", original, before);
        } catch (Failure f) {
            denied = true;
        }

        check(denied, "cross-version profile accepted");
        denied = false;

        try {
            LauncherCanbusGuard.validate(PROFILE, 30, "arm64-v8a", Path.of(a[1]), before);
        } catch (Failure f) {
            denied = true;
        }

        check(denied, "cross-version apk accepted");
        System.out.println(
                "PASS CAN snapshot deferred to genuine successful init callback; disconnected paths"
                        + " gated; unrelated classes preserved");
    }

    static void checkUnrelatedPreserved(Map<String, ClassDef> old, Map<String, ClassDef> out)
            throws Exception {
        check(old.keySet().equals(out.keySet()), "class set drift");

        for (var e : old.entrySet()) {
            if (!e.getKey().equals(LauncherCanbusGuard.OWNER)) {
                check(
                        FrameworkDexMaterializer.semantic(e.getValue())
                                .equals(FrameworkDexMaterializer.semantic(out.get(e.getKey()))),
                        "unrelated class drift");
            }
        }

        for (var m : old.get(LauncherCanbusGuard.OWNER).getMethods()) {
            if (!Set.of(LauncherCanbusGuard.REGISTER, LauncherCanbusGuard.CALLBACK)
                    .contains(m.getName())) {
                Method same = null;

                for (var n : out.get(LauncherCanbusGuard.OWNER).getMethods()) {
                    if (n.getName().equals(m.getName())
                            && n.getParameterTypes().equals(m.getParameterTypes())
                            && n.getReturnType().equals(m.getReturnType())) {
                        same = n;
                    }
                }

                check(
                        same != null
                                && FrameworkDexMaterializer.semantic(m)
                                        .equals(FrameworkDexMaterializer.semantic(same)),
                        "unrelated owner method drift: " + m.getName());
            }
        }
    }
}
