package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Field;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.reference.FieldReference;
import org.jf.dexlib2.iface.reference.MethodReference;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.TreeMap;
import java.util.zip.ZipFile;

public final class HiboardTopGestureGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static void check(boolean b, String why) {
        if (!b) {
            throw new AssertionError(why);
        }
    }

    static void reject(Action a) throws Exception {
        try {
            a.run();
            throw new AssertionError("Invalid boundary accepted");
        } catch (Failure e) {
            check(e.code.equals("HIBOARD_TOP_GESTURE"), e.code);
        }
    }

    static Method target(Map<String, ClassDef> classes) {
        for (var c : classes.values()) {
            for (var m : c.getMethods()) {
                if (m.toString().equals(HiboardTopGestureGuard.TARGET)) {
                    return m;
                }
            }
        }

        throw new AssertionError("target missing");
    }

    static Method mutate(Method m, int mode) {
        var code = new MutableMethodImplementation(m.getImplementation());
        int at = 0;

        for (int i = 0; i < code.getInstructions().size(); i++) {
            var x = code.getInstructions().get(i);

            if (at == 0x17) {
                if (mode == 0) {
                    code.replaceInstruction(i, new BuilderInstruction10x(Opcode.NOP));
                } else {
                    code.replaceInstruction(
                            i,
                            new BuilderInstruction35c(
                                    Opcode.INVOKE_STATIC,
                                    2,
                                    mode == 1 ? 2 : 1,
                                    2,
                                    0,
                                    0,
                                    0,
                                    new ImmutableMethodReference(
                                            mode == 2
                                                    ? "Lforeign/Policy;"
                                                    : HiboardTopGestureGuard.POLICY,
                                            "access$400",
                                            HiboardTopGestureGuard.PARAMETERS,
                                            "V")));
                }

                break;
            }

            at += x.getCodeUnits();
        }

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                code);
    }

    static Method resolveMethod(Map<String, ClassDef> classes, String owner, String signature) {
        for (String at = owner;
                at != null && classes.containsKey(at);
                at = classes.get(at).getSuperclass()) {
            for (var m : classes.get(at).getMethods()) {
                if (m.toString().substring(m.toString().indexOf("->")).equals(signature)) {
                    return m;
                }
            }
        }

        return null;
    }

    static Field resolveField(Map<String, ClassDef> classes, String owner, String signature) {
        for (String at = owner;
                at != null && classes.containsKey(at);
                at = classes.get(at).getSuperclass()) {
            for (var f : classes.get(at).getFields()) {
                if (f.toString().substring(f.toString().indexOf("->")).equals(signature)) {
                    return f;
                }
            }
        }

        return null;
    }

    static int symbols(Map<String, ClassDef> platform, Map<String, ClassDef> helper)
            throws Exception {
        var all = new TreeMap<String, ClassDef>(platform);

        all.putAll(helper);
        int refs = 0;

        for (var c : helper.values()) {
            for (var m : c.getMethods()) {
                if (m.getImplementation() != null) {
                    for (var i : m.getImplementation().getInstructions()) {
                        if (i instanceof ReferenceInstruction ri) {
                            var r = ri.getReference();

                            if (r instanceof MethodReference mr
                                    && mr.getDefiningClass()
                                            .startsWith("Lcom/android/server/wm/")) {
                                String s = mr.toString();
                                var found =
                                        resolveMethod(
                                                all,
                                                mr.getDefiningClass(),
                                                s.substring(s.indexOf("->")));
                                check(found != null, "Actual services method not resolved: " + s);
                                check(
                                        (found.getAccessFlags() & 2) == 0
                                                || helper.containsKey(found.getDefiningClass()),
                                        "Foreign private method");

                                if (mr.getName().startsWith("access$")) {
                                    check(
                                            (found.getAccessFlags() & 8) != 0,
                                            "Original synthetic accessor static");
                                }

                                refs++;
                            }

                            if (r instanceof FieldReference fr
                                    && fr.getDefiningClass()
                                            .startsWith("Lcom/android/server/wm/")) {
                                String s = fr.toString();
                                var found =
                                        resolveField(
                                                all,
                                                fr.getDefiningClass(),
                                                s.substring(s.indexOf("->")));
                                check(found != null, "Actual services field not resolved: " + s);
                                check(
                                        (found.getAccessFlags() & 2) == 0
                                                || helper.containsKey(found.getDefiningClass()),
                                        "Foreign private field");
                                refs++;
                            }
                        }
                    }
                }
            }
        }

        return refs;
    }

    public static void main(String[] a) throws Exception {
        Path original = Path.of(a[0]), destination = Path.of(a[2]);

        Files.createDirectories(destination);
        byte[] clean;

        try (var z = new ZipFile(original.toFile())) {
            clean = z.getInputStream(z.getEntry("classes2.dex")).readAllBytes();
        }

        byte[] predecessor = Api30SecondaryNavigationPolicy.apply(30, original, clean);
        var before = HiboardTopGestureGuard.classes(predecessor);
        byte[] helper = Files.readAllBytes(Path.of(a[1]));

        if (a.length > 3 && a[3].equals("--original")) {
            var m = target(before);
            boolean stock = false;

            for (var i : m.getImplementation().getInstructions()) {
                stock |=
                        i instanceof ReferenceInstruction r
                                && r.getReference()
                                        .toString()
                                        .equals(HiboardTopGestureGuard.ref(false));
            }

            check(stock, "RED original top callback still calls stock transient request");

            return;
        }

        int resolved = symbols(before, HiboardTopGestureGuard.classes(helper));
        byte[] output =
                HiboardTopGestureGuard.apply(
                        HiboardTopGestureGuard.PROFILE, 30, original, predecessor, helper);
        var after = HiboardTopGestureGuard.classes(output);
        int changed = 0;

        for (var e : before.entrySet()) {
            if (!FrameworkDexMaterializer.semantic(e.getValue())
                    .equals(FrameworkDexMaterializer.semantic(after.get(e.getKey())))) {
                check(
                        e.getKey().equals(HiboardTopGestureGuard.OWNER),
                        "Only top callback owner changed");
                changed++;
            }
        }

        check(
                changed == 1 && after.size() == before.size() + 2,
                "One original class changed and exactly two owned helper classes added");
        var old = target(before);
        var now = target(after);
        check(
                FrameworkDexMaterializer.semantic(old)
                        .equals(
                                FrameworkDexMaterializer.semantic(
                                        HiboardTopGestureGuard.rewrite(now, false))),
                "Exact callback inverse including original try/monitor");
        int at = 0, sites = 0;
        var x = old.getImplementation().getInstructions().iterator();
        var y = now.getImplementation().getInstructions().iterator();

        while (x.hasNext()) {
            var aa = x.next();
            var bb = y.next();
            check(aa.getCodeUnits() == bb.getCodeUnits(), "Same-width instructions");

            if (at == 0x17) {
                check(
                        ((ReferenceInstruction) bb)
                                .getReference()
                                .toString()
                                .equals(HiboardTopGestureGuard.ref(true)),
                        "Exact own helper route");
                sites++;
            } else {
                check(
                        FrameworkDexMaterializer.semantic(aa)
                                .equals(FrameworkDexMaterializer.semantic(bb)),
                        "Other callback instruction unchanged");
            }

            at += aa.getCodeUnits();
        }

        check(
                !y.hasNext() && at == 31 && sites == 1,
                "Exact single call, original registers and branch offsets");
        reject(() ->
                HiboardTopGestureGuard.apply(
                        "passion-11-russian-20260403-2.1.4.80", 30, original, predecessor, helper));
        reject(() ->
                HiboardTopGestureGuard.apply(
                        "free-09-chinese-20260106-6.0.3", 30, original, predecessor, helper));
        reject(() ->
                HiboardTopGestureGuard.apply(
                        HiboardTopGestureGuard.PROFILE, 28, original, predecessor, helper));
        reject(() ->
                HiboardTopGestureGuard.apply(
                        HiboardTopGestureGuard.PROFILE, 30, original, clean, helper));
        reject(() ->
                HiboardTopGestureGuard.apply(
                        HiboardTopGestureGuard.PROFILE, 30, original, predecessor, output));
        reject(() ->
                HiboardTopGestureGuard.apply(
                        HiboardTopGestureGuard.PROFILE, 30, original, output, helper));
        Path wrong = destination.resolve("wrong.jar");
        Files.writeString(wrong, "wrong source");
        reject(() ->
                HiboardTopGestureGuard.apply(
                        HiboardTopGestureGuard.PROFILE, 30, wrong, predecessor, helper));

        for (int mode = 0; mode < 3; mode++) {
            var m = mutate(old, mode);
            reject(() -> HiboardTopGestureGuard.validateMethod(m));
            reject(() -> HiboardTopGestureGuard.rewrite(m, true));
        }

        Files.write(destination.resolve("classes2.dex"), output);
        Files.writeString(
                destination.resolve("receipt.json"),
                Json.write(
                                Map.of(
                                        "single_samewidth_call",
                                        true,
                                        "offset",
                                        23,
                                        "callback_code_units",
                                        31,
                                        "whole_inverse_original_classes",
                                        true,
                                        "original_classes",
                                        before.size(),
                                        "helper_classes",
                                        2,
                                        "services_symbol_references_resolved",
                                        resolved,
                                        "guard_negative_cases",
                                        14,
                                        "profile_activated",
                                        false,
                                        "ART_verified",
                                        false))
                        + "\n");
        System.out.println(
                "GREEN one same-width call, whole-original-class inverse, "
                        + resolved
                        + " actual services references, 14 guard negatives; ART pending");
    }
}
