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

/**
 * Private five-site status factory/receiver/service lifecycle boundary; existing navigation code
 * unchanged.
 */
final class PassionStatusReadyGuard {
    static final String APK = "4032f14c0fc49fd52514737309d2af67c9d4ff9340dd534874f46cbe2e748c00",
            HELPER = "Lcom/voyemu/output/PassionStatusReady;";

    record Site(int start, int end, int invoke, int receiver, int second, String helper) {}

    record Target(
            String owner, String name, int registers, List<String> parameters, List<Site> sites) {}

    static final List<Target> TARGETS =
            List.of(
                    new Target(
                            "Lcom/voyah/os/carlauncher/LauncherApplication;",
                            "onResumed",
                            5,
                            List.of(),
                            List.of(new Site(65, 68, 65, 4, -1, "prepare"))),
                    new Target(
                            "Lcom/voyah/os/carsystemui/SystemuiService;",
                            "onCreate",
                            4,
                            List.of(),
                            List.of(new Site(30, 40, 30, 3, -1, "startService"))),
                    new Target(
                            "Lcom/voyah/os/carsystemui/SystemuiService;",
                            "onDestroy",
                            3,
                            List.of(),
                            List.of(new Site(8, 13, 10, 2, -1, "destroyService"))),
                    new Target(
                            "Lcom/voyah/os/carsystemui/SystemuiService$1;",
                            "onReceive",
                            5,
                            List.of("Landroid/content/Context;", "Landroid/content/Intent;"),
                            List.of(
                                    new Site(55, 62, 55, 2, 0, "showService"),
                                    new Site(85, 92, 85, 2, -1, "hideService"))));

    static void require(boolean b) {
        Failure.require(
                b,
                "PASSION_STATUS_READY_CONTRACT",
                "Exact five-site Passion status readiness contract required",
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
                m.getParameterTypes().equals(t.parameters)
                        && m.getReturnType().equals("V")
                        && m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == t.registers);
    }

    static Site site(Target t, int at) {
        for (var s : t.sites) {
            if (at >= s.start && at < s.end) {
                return s;
            }
        }

        return null;
    }

    static Method rewrite(Method method, Method original, Target t, boolean inverse) {
        var body = method.getImplementation();
        var instructions = new ArrayList<Instruction>();
        int at = 0;

        for (var i : body.getInstructions()) {
            Site s = site(t, at);

            if (inverse) {
                if (s != null && at == s.start) {
                    int rawAt = 0;

                    for (var raw : original.getImplementation().getInstructions()) {
                        if (rawAt >= s.start && rawAt < s.end) {
                            instructions.add(raw);
                        }

                        rawAt += raw.getCodeUnits();
                    }
                }

                if (s == null) {
                    instructions.add(i);
                }
            } else if (s == null) {
                instructions.add(i);
            } else {
                for (int n = 0; n < i.getCodeUnits(); n++) {
                    if (at + n == s.invoke) {
                        var args =
                                s.second < 0
                                        ? List.of("Landroid/content/Context;")
                                        : List.of("Landroid/content/Context;", "Z");
                        instructions.add(
                                new ImmutableInstruction35c(
                                        Opcode.INVOKE_STATIC,
                                        args.size(),
                                        s.receiver,
                                        s.second < 0 ? 0 : s.second,
                                        0,
                                        0,
                                        0,
                                        new ImmutableMethodReference(HELPER, s.helper, args, "V")));
                        n += 2;
                    } else {
                        instructions.add(new ImmutableInstruction10x(Opcode.NOP));
                    }
                }
            }

            at += i.getCodeUnits();
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

        require(count == 4);

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
        prior = PassionNavigationReadyGuard.applyPrepared(profile, api, original, prior);
        validatePrepared(profile, api, original, prior);
    }

    static void validateHelperSource(Path source) throws Exception {
        var files = FilesSupport.tree(source);

        require(files.keySet().equals(Set.of("com/voyemu/output/PassionStatusReady.java")));
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
