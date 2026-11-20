package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction35c;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/** Private equal-width three-site factory/show + real service field lifecycle boundary. */
final class PassionNavigationReadyGuard {
    static final String APK = "4032f14c0fc49fd52514737309d2af67c9d4ff9340dd534874f46cbe2e748c00",
            HELPER = "Lcom/voyemu/output/PassionNavigationReady;";

    record Target(
            String owner,
            String name,
            int registers,
            int start,
            int end,
            int invoke,
            int receiver,
            String helper) {}

    static final List<Target> TARGETS =
            List.of(
                    new Target(
                            "Lcom/voyah/os/carlauncher/LauncherActivityMain;",
                            "showView",
                            4,
                            146,
                            153,
                            146,
                            3,
                            "show"),
                    new Target(
                            "Lcom/voyah/os/carsystemui/SystemuiService;",
                            "onCreate",
                            4,
                            40,
                            49,
                            40,
                            3,
                            "showService"),
                    new Target(
                            "Lcom/voyah/os/carsystemui/SystemuiService;",
                            "onDestroy",
                            3,
                            13,
                            18,
                            15,
                            2,
                            "destroyService"));

    static void require(boolean b) {
        Failure.require(
                b,
                "PASSION_NAVIGATION_READY_CONTRACT",
                "Exact three-site Passion navigation readiness contract required",
                3);
    }

    static Target target(Method m) {
        for (var t : TARGETS) {
            if (m.getDefiningClass().equals(t.owner) && m.getName().equals(t.name)) {
                return t;
            }
        }

        return null;
    }

    static void validateMethod(Method m, Target t) throws Exception {
        require(
                m.getParameterTypes().isEmpty()
                        && m.getReturnType().equals("V")
                        && m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == t.registers);
    }

    static Method rewrite(Method method, Method original, Target t, boolean inverse) {
        var body = method.getImplementation();
        var instructions = new ArrayList<Instruction>();
        int at = 0;

        if (inverse) {
            boolean inserted = false;

            for (var i : body.getInstructions()) {
                if (at == t.start) {
                    int rawAt = 0;

                    for (var raw : original.getImplementation().getInstructions()) {
                        if (rawAt >= t.start && rawAt < t.end) {
                            instructions.add(raw);
                        }

                        rawAt += raw.getCodeUnits();
                    }

                    inserted = true;
                }

                if (at < t.start || at >= t.end) {
                    instructions.add(i);
                }

                at += i.getCodeUnits();
            }

            require(inserted);
        } else {
            for (var i : body.getInstructions()) {
                if (at >= t.start && at < t.end) {
                    for (int n = 0; n < i.getCodeUnits(); n++) {
                        int addr = at + n;

                        if (addr == t.invoke) {
                            instructions.add(
                                    new ImmutableInstruction35c(
                                            Opcode.INVOKE_STATIC,
                                            1,
                                            t.receiver,
                                            0,
                                            0,
                                            0,
                                            0,
                                            new ImmutableMethodReference(
                                                    HELPER,
                                                    t.helper,
                                                    List.of("Landroid/content/Context;"),
                                                    "V")));
                            n += 2;
                        } else {
                            instructions.add(new ImmutableInstruction10x(Opcode.NOP));
                        }
                    }
                } else {
                    instructions.add(i);
                }

                at += i.getCodeUnits();
            }
        }

        return new ImmutableMethod(
                method.getDefiningClass(),
                method.getName(),
                method.getParameters(),
                method.getReturnType(),
                method.getAccessFlags(),
                method.getAnnotations(),
                method.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(
                        body.getRegisterCount(),
                        instructions,
                        body.getTryBlocks(),
                        body.getDebugItems()));
    }

    static Method find(Map<String, ClassDef> classes, Target t) {
        for (var m : classes.get(t.owner).getMethods()) {
            if (t.equals(target(m))) {
                return m;
            }
        }

        throw new IllegalStateException("missing method");
    }

    static Map<String, ClassDef> transform(
            Map<String, ClassDef> input, Map<String, ClassDef> original, boolean inverse)
            throws Exception {
        var out = new TreeMap<String, ClassDef>();
        int count = 0;

        for (var c : input.values()) {
            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();
            int g = 0;

            for (var group : List.of(c.getDirectMethods(), c.getVirtualMethods())) {
                var dest = g++ == 0 ? direct : virtual;

                for (var m : group) {
                    var t = target(m);
                    Method next = m;

                    if (t != null) {
                        Method baseline = find(original, t);
                        validateMethod(baseline, t);

                        if (!inverse) {
                            validateMethod(m, t);
                        }

                        next = rewrite(m, baseline, t, inverse);

                        if (inverse) {
                            validateMethod(next, t);
                        }

                        count++;
                    }

                    dest.add(next);
                }
            }

            out.put(
                    c.getType(),
                    new ImmutableClassDef(
                            c.getType(),
                            c.getAccessFlags(),
                            c.getSuperclass(),
                            c.getInterfaces(),
                            c.getSourceFile(),
                            c.getAnnotations(),
                            c.getStaticFields(),
                            c.getInstanceFields(),
                            direct,
                            virtual));
        }

        require(count == 3);

        return out;
    }

    static void validateAfter(Method before, Method after, Target t) throws Exception {
        validateMethod(before, t);
        require(
                FrameworkDexMaterializer.semantic(rewrite(before, before, t, false))
                        .equals(FrameworkDexMaterializer.semantic(after)));
        require(
                FrameworkDexMaterializer.semantic(rewrite(after, before, t, true))
                        .equals(FrameworkDexMaterializer.semantic(before)));
    }

    static void validatePrepared(String profile, int api, Path original, byte[] dex)
            throws Exception {
        require(api == 30 && FilesSupport.hash(original).equals(APK));
        var classes = PassionServiceForegroundGuard.classes(dex);
        transform(classes, classes, false);
    }

    static void validateOriginalChain(String profile, int api, Path original, byte[] raw)
            throws Exception {
        require(api == 30 && FilesSupport.hash(original).equals(APK));
        byte[] prior = PassionSidebarGuard.apply(profile, api, original, raw);
        prior = PassionBluetoothVolumeGuard.apply(profile, api, original, prior);
        prior = PassionLauncherAudioGuard.apply(profile, api, original, prior);
        validatePrepared(profile, api, original, prior);
    }

    static void validateHelperSource(Path source) throws Exception {
        var files = FilesSupport.tree(source);

        require(files.keySet().equals(Set.of("com/voyemu/output/PassionNavigationReady.java")));
    }

    static byte[] applyPrepared(String profile, int api, Path apk, byte[] dex) throws Exception {
        require(api == 30 && FilesSupport.hash(apk).equals(APK));
        var before = PassionServiceForegroundGuard.classes(dex);
        var expected = transform(before, before, false);
        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), expected.values()));
        var actual = PassionServiceForegroundGuard.classes(output);
        require(actual.keySet().equals(before.keySet()));

        for (var name : before.keySet()) {
            require(
                    FrameworkDexMaterializer.semantic(expected.get(name))
                            .equals(FrameworkDexMaterializer.semantic(actual.get(name))));
        }

        var restored = transform(actual, before, true);

        for (var name : before.keySet()) {
            require(
                    FrameworkDexMaterializer.semantic(before.get(name))
                            .equals(FrameworkDexMaterializer.semantic(restored.get(name))));
        }

        return output;
    }
}
