package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Map;
import java.util.TreeMap;
import java.util.zip.ZipFile;

public final class HiboardRoutingTest {
    static java.util.List<HiboardRouting.Body> bodies;

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
            throw new AssertionError("Wrong Hiboard boundary accepted");
        } catch (Failure e) {
            check(e.code.equals("HIBOARD_ROUTING"), "Exact contract failure");
        }
    }

    static Map<String, ClassDef> classes(byte[] dex) {
        var out = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            check(out.put(c.getType(), c) == null, "No duplicate class");
        }

        return out;
    }

    static Method method(byte[] dex, String name) {
        for (var c : classes(dex).values()) {
            for (var m : c.getMethods()) {
                if (m.toString().equals(name)) {
                    return m;
                }
            }
        }

        throw new AssertionError("Missing target");
    }

    static Method mutate(Method m, int mode) {
        var body = HiboardRouting.body(bodies, m);
        var code = new MutableMethodImplementation(m.getImplementation());
        int address = 0;

        for (int i = 0; i < code.getInstructions().size(); i++) {
            var old = code.getInstructions().get(i);

            if (address == body.sites().get(0).address()) {
                var r = (FiveRegisterInstruction) old;
                var s = body.sites().get(0);

                if (mode == 0) {
                    code.replaceInstruction(
                            i,
                            new BuilderInstruction35c(
                                    Opcode.INVOKE_INTERFACE,
                                    r.getRegisterCount(),
                                    (r.getRegisterC() + 1)
                                            % m.getImplementation().getRegisterCount(),
                                    r.getRegisterD(),
                                    r.getRegisterE(),
                                    r.getRegisterF(),
                                    r.getRegisterG(),
                                    new ImmutableMethodReference(
                                            HiboardRouting.MANAGER,
                                            s.name(),
                                            s.parameters(false),
                                            "V")));
                } else if (mode == 1) {
                    code.replaceInstruction(i, new BuilderInstruction10x(Opcode.NOP));
                } else {
                    code.replaceInstruction(
                            i,
                            new BuilderInstruction35c(
                                    Opcode.INVOKE_INTERFACE,
                                    r.getRegisterCount(),
                                    r.getRegisterC(),
                                    r.getRegisterD(),
                                    r.getRegisterE(),
                                    r.getRegisterF(),
                                    r.getRegisterG(),
                                    new ImmutableMethodReference(
                                            "Landroid/view/OtherManager;",
                                            s.name(),
                                            s.parameters(false),
                                            "V")));
                }

                break;
            }

            address += old.getCodeUnits();
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

    static void verify(byte[] source, byte[] patched) throws Exception {
        var a = classes(source);
        var b = classes(patched);

        check(a.keySet().equals(b.keySet()), "Class set preserved");
        int methods = 0, sites = 0;

        for (var c : a.values()) {
            for (var m : c.getMethods()) {
                var spec = HiboardRouting.body(bodies, m);

                if (spec == null) {
                    continue;
                }

                var after = method(patched, m.toString());
                check(
                        m.getAccessFlags() == after.getAccessFlags()
                                && m.getImplementation().getRegisterCount()
                                        == after.getImplementation().getRegisterCount(),
                        "Registers and access preserved");
                var original = new ArrayList<Instruction>();
                m.getImplementation().getInstructions().forEach(original::add);
                var changed = new ArrayList<Instruction>();
                after.getImplementation().getInstructions().forEach(changed::add);
                check(original.size() == changed.size(), "No new gesture/timer instructions");
                int address = 0;

                for (int i = 0; i < original.size(); i++) {
                    var x = original.get(i);
                    var y = changed.get(i);
                    final int at = address;
                    var site = spec.sites().stream().filter(s -> s.address() == at).findFirst();
                    check(x.getCodeUnits() == y.getCodeUnits(), "Branch addresses preserved");

                    if (site.isPresent()) {
                        check(
                                y.getOpcode() == Opcode.INVOKE_STATIC
                                        && ((ReferenceInstruction) y)
                                                .getReference()
                                                .toString()
                                                .equals(site.get().reference(true)),
                                "Original lifecycle call routed");
                        sites++;
                    } else {
                        check(
                                FrameworkDexMaterializer.semantic(x)
                                        .equals(FrameworkDexMaterializer.semantic(y)),
                                "All OEM instructions retained");
                    }

                    address += x.getCodeUnits();
                }

                check(
                        FrameworkDexMaterializer.semantic(m)
                                .equals(
                                        FrameworkDexMaterializer.semantic(
                                                HiboardRouting.rewrite(after, spec, false))),
                        "Exact inverse original method");
                methods++;
            }
        }

        check(methods == 28 && sites == 38, "Finite source-derived panel/delegate/dim call sites");

        for (var e : a.entrySet()) {
            var before = e.getValue();
            var after = b.get(e.getKey());
            boolean target = false;

            for (var m : before.getMethods()) {
                if (HiboardRouting.body(bodies, m) != null) {
                    target = true;
                }
            }

            if (!target) {
                check(
                        FrameworkDexMaterializer.semantic(before)
                                .equals(FrameworkDexMaterializer.semantic(after)),
                        "Unrelated libraries/classes unchanged");
            }
        }
    }

    public static void main(String[] args) throws Exception {
        Path original = Path.of(args[0]), stage = Path.of(args[1]);
        Map<String, Object> constants =
                TestProfiles.constants(Path.of("").toAbsolutePath(), HiboardRouting.NAME, original);

        bodies = HiboardRouting.bodies(constants);

        Files.createDirectories(stage);
        byte[] dex;
        var entries = new TreeMap<String, byte[]>();

        try (var z = new ZipFile(original.toFile())) {
            for (var e : Collections.list(z.entries())) {
                if (!e.isDirectory()) {
                    entries.put(e.getName(), z.getInputStream(e).readAllBytes());
                }
            }
        }

        dex = entries.get("classes.dex");

        if (args.length == 3 && args[2].equals("--original")) {
            verify(dex, dex);

            return;
        }

        byte[] output = HiboardRouting.apply(constants, 30, original, dex);
        verify(dex, output);
        reject(() -> HiboardRouting.apply(constants, 28, original, dex));
        reject(() -> HiboardRouting.apply(constants, 30, original, null));
        reject(() -> HiboardRouting.apply(constants, 30, original, output));
        Path wrong = stage.resolve("wrong.apk");
        Files.writeString(wrong, "wrong original");
        reject(() -> HiboardRouting.apply(constants, 30, wrong, dex));
        Method sample = method(dex, bodies.get(0).method());
        var spec = HiboardRouting.body(bodies, sample);

        for (int mode = 0; mode < 3; mode++) {
            var changed = mutate(sample, mode);
            reject(() -> HiboardRouting.validateMethod(changed, spec));
            reject(() -> HiboardRouting.rewrite(changed, spec, true));
        }

        var result = new TreeMap<String, byte[]>(entries);
        result.put("classes.dex", output);
        int changed = 0;

        for (var e : entries.entrySet()) {
            if (!Arrays.equals(e.getValue(), result.get(e.getKey()))) {
                check(
                        e.getKey().equals("classes.dex"),
                        "Every resource, native library, manifest and classes2.dex preserved");
                changed++;
            }
        }

        check(changed == 1, "Only classes.dex changed");
        Files.write(stage.resolve("classes.dex"), output);
        Files.writeString(
                stage.resolve("receipt.json"),
                Json.write(
                                Map.of(
                                        "source_RED_routed_GREEN",
                                        true,
                                        "methods",
                                        28,
                                        "sites",
                                        38,
                                        "negative_cases",
                                        11,
                                        "inverse_all_classes_exact",
                                        true,
                                        "other_APK_entries_exact",
                                        true,
                                        "classes2_untouched",
                                        true,
                                        "runtime_verified",
                                        false))
                        + "\n");
        System.out.println(
                "Hiboard source RED -> routed GREEN: 28 methods, 38 lifecycle sites, 12 negatives;"
                        + " inverse OEM semantics and all other APK entries preserved; runtime"
                        + " pending");
    }
}
