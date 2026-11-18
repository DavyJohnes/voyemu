package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11n;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Private exact null boundaries; no fabricated ECU version or successful Nexus write. */
final class PassionGuestNullGuards {
    static final String PROFILE = "passion-11-russian-20260403-2.1.4.80";

    record Target(String role, String owner, String method, String apk, int registers) {}

    static final Target ECU =
            new Target(
                    "LY_Car",
                    "Lcom/voyah/platform/carproperty/api/ECUSettings;",
                    "getHardwareVersion",
                    "a5fb5daed4a295a568daf0390858a0bd00782b1384e66b13a96f66d43ad43e60",
                    2);
    static final Target AVM =
            new Target(
                    "LY_AVM",
                    "Lcom/mega/nexus/app/MegaActivityManager;",
                    "setFlyingScreenMode",
                    "ca78d415246c5be5c892d93a1b90c9dc19a638024af3ec61877c309cee7b8ecb",
                    4);

    static void require(boolean b) {
        Failure.require(
                b,
                "PASSION_GUEST_NULL_CONTRACT",
                "Exact pinned optional ECU/Nexus null boundary required",
                3);
    }

    static boolean selected(Method m, Target t) {
        return m.getDefiningClass().equals(t.owner) && m.getName().equals(t.method);
    }

    static void validateMethod(Method m, Target t) throws Exception {
        require(
                m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == t.registers);
    }

    static Method rewrite(Method m, Target t, boolean inverse) {
        var b = new MutableMethodImplementation(m.getImplementation());

        if (ECU.equals(t)) {
            if (inverse) {
                for (int address : new int[] {23, 15}) {
                    int i = PassionServiceForegroundGuard.index(b, address);

                    for (int n = 0; n < 3; n++) {
                        b.removeInstruction(i);
                    }
                }
            } else {
                for (int address : new int[] {19, 15}) {
                    int i = PassionServiceForegroundGuard.index(b, address);
                    var next = b.newLabelForAddress(address);
                    b.addInstruction(i, new BuilderInstruction21t(Opcode.IF_NEZ, 0, next));
                    b.addInstruction(i + 1, new BuilderInstruction11n(Opcode.CONST_4, 0, 0));
                    b.addInstruction(i + 2, new BuilderInstruction11x(Opcode.RETURN_OBJECT, 0));
                }
            }
        } else {
            int i = PassionServiceForegroundGuard.index(b, 4);

            if (inverse) {
                for (int n = 0; n < 3; n++) {
                    b.removeInstruction(i);
                }
            } else {
                var next = b.newLabelForAddress(4);
                b.addInstruction(i, new BuilderInstruction21t(Opcode.IF_NEZ, 2, next));
                b.addInstruction(i + 1, new BuilderInstruction11n(Opcode.CONST_4, 2, 0));
                b.addInstruction(i + 2, new BuilderInstruction11x(Opcode.RETURN, 2));
            }
        }

        return PassionServiceForegroundGuard.copy(m, b);
    }

    static Map<String, ClassDef> transform(Map<String, ClassDef> input, Target t, boolean inverse)
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
                    Method next = m;

                    if (selected(m, t)) {
                        if (!inverse) {
                            validateMethod(m, t);
                        }

                        next = rewrite(m, t, inverse);

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

        require(count == 1);

        return out;
    }

    static void validateAfter(Method before, Method after, Target t) throws Exception {
        validateMethod(before, t);
        require(
                FrameworkDexMaterializer.semantic(rewrite(before, t, false))
                        .equals(FrameworkDexMaterializer.semantic(after)));
        require(
                FrameworkDexMaterializer.semantic(rewrite(after, t, true))
                        .equals(FrameworkDexMaterializer.semantic(before)));
    }

    static void validatePrepared(String profile, int api, Path original, byte[] dex, Target t)
            throws Exception {
        require(PROFILE.equals(profile) && api == 30 && FilesSupport.hash(original).equals(t.apk));
        transform(PassionServiceForegroundGuard.classes(dex), t, false);
    }

    static void validateOriginalChain(String profile, int api, Path original, byte[] raw, Target t)
            throws Exception {
        require(PROFILE.equals(profile) && api == 30 && FilesSupport.hash(original).equals(t.apk));
        byte[] prior = raw;

        if (ECU.equals(t)) {
            prior = PassionVehicleBluetoothGuard.apply(profile, api, original, raw);
            prior = PassionAmbientLightGuard.apply(profile, api, original, prior);
        }

        validatePrepared(profile, api, original, prior, t);
    }

    static byte[] applyPrepared(String profile, int api, Path original, byte[] dex, Target t)
            throws Exception {
        require(PROFILE.equals(profile) && api == 30 && FilesSupport.hash(original).equals(t.apk));
        var before = PassionServiceForegroundGuard.classes(dex);
        var expected = transform(before, t, false);
        byte[] result =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), expected.values()));
        var actual = PassionServiceForegroundGuard.classes(result);
        require(actual.keySet().equals(before.keySet()));

        for (var name : expected.keySet()) {
            require(
                    FrameworkDexMaterializer.semantic(expected.get(name))
                            .equals(FrameworkDexMaterializer.semantic(actual.get(name))));
        }

        var restored = transform(actual, t, true);

        for (var name : before.keySet()) {
            require(
                    FrameworkDexMaterializer.semantic(before.get(name))
                            .equals(FrameworkDexMaterializer.semantic(restored.get(name))));
        }

        return result;
    }
}
