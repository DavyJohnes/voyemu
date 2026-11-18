package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.BuilderInstruction;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11n;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.reference.ImmutableFieldReference;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableTypeReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Guard the reproduced optional ambient service without inventing light state. */
final class PassionAmbientLightGuard {
    static final String PROFILE = PassionServiceForegroundGuard.PROFILE,
            NAME = "passion-api30-ambient-light",
            PIN = "a5fb5daed4a295a568daf0390858a0bd00782b1384e66b13a96f66d43ad43e60";
    static final String OWNER = "Lcom/voyah/platform/amblight/api/AmbientLightSettings;",
            FRAGMENT = "Lcom/voyah/car/ui/body/LightingFragmentN;",
            MODEL = "Lcom/voyah/car/vm/body/LightingViewModel;",
            HELPER = "Lcom/voyemu/window/PassionAmbientLight;";

    record Target(String owner, String method, int registers, int kind) {}

    static final List<Target> TARGETS =
            List.of(
                    new Target(
                            "Lcom/voyah/platform/amblight/api/AmbientLightSettings;",
                            "getColorValue",
                            1,
                            0),
                    new Target(
                            "Lcom/voyah/platform/amblight/api/AmbientLightSettings;",
                            "getLightMode",
                            1,
                            0),
                    new Target(
                            "Lcom/voyah/platform/amblight/api/AmbientLightSettings;",
                            "getRhythmModeColorSelectType",
                            1,
                            0),
                    new Target(
                            "Lcom/voyah/platform/amblight/api/AmbientLightSettings;",
                            "setBrightnessValue",
                            4,
                            1),
                    new Target(
                            "Lcom/voyah/platform/amblight/api/AmbientLightSettings;",
                            "setColorValue",
                            4,
                            1),
                    new Target(
                            "Lcom/voyah/platform/amblight/api/AmbientLightSettings;",
                            "setLightMode",
                            4,
                            1),
                    new Target(
                            "Lcom/voyah/platform/amblight/api/AmbientLightSettings;",
                            "setRhythmModeColorSelectType",
                            4,
                            1),
                    new Target(
                            "Lcom/voyah/platform/amblight/api/AmbientLightSettings;",
                            "updateData",
                            5,
                            2),
                    new Target("Lcom/voyah/car/ui/body/LightingFragmentN;", "initObserver", 3, 4),
                    new Target("Lcom/voyah/car/ui/body/LightingFragmentN;", "initView", 8, 3),
                    new Target(
                            "Lcom/voyah/car/ui/body/LightingFragmentN;",
                            "lambda$initObserver$0$LightingFragmentN",
                            4,
                            5));

    static void require(boolean b) {
        Failure.require(
                b,
                "PASSION_AMBIENT_LIGHT_CONTRACT",
                "Exact optional ambient service contract required",
                3);
    }

    static Target target(Method m) {
        for (var t : TARGETS) {
            if (m.getDefiningClass().equals(t.owner)
                    && m.getName().equals(t.method)
                    && !(m.getName().equals("updateData")
                            && m.getParameterTypes().equals(List.of("Ljava/lang/Object;")))) {
                return t;
            }
        }

        return null;
    }

    static BuilderInstruction22c field(
            int dest, int owner, String type, String name, String value) {
        return new BuilderInstruction22c(
                Opcode.IGET_OBJECT, dest, owner, new ImmutableFieldReference(type, name, value));
    }

    static BuilderInstruction35c invoke(
            int opcode, int count, int a, int b, ImmutableMethodReference method) {
        return new BuilderInstruction35c(
                opcode == 0 ? Opcode.INVOKE_VIRTUAL : Opcode.INVOKE_STATIC,
                count,
                a,
                b,
                0,
                0,
                0,
                method);
    }

    static List<BuilderInstruction> availability(int owner, int root) {
        return List.of(
                field(0, owner, FRAGMENT, "mViewModel", "Landroidx/lifecycle/ViewModel;"),
                new BuilderInstruction21c(Opcode.CHECK_CAST, 0, new ImmutableTypeReference(MODEL)),
                invoke(
                        0,
                        1,
                        0,
                        0,
                        new ImmutableMethodReference(MODEL, "getLightMode", List.of(), "I")),
                new BuilderInstruction11x(Opcode.MOVE_RESULT, 0),
                field(root, owner, FRAGMENT, "mBinding", "Landroidx/databinding/ViewDataBinding;"),
                invoke(
                        0,
                        1,
                        root,
                        0,
                        new ImmutableMethodReference(
                                "Landroidx/databinding/ViewDataBinding;",
                                "getRoot",
                                List.of(),
                                "Landroid/view/View;")),
                new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, root),
                invoke(
                        1,
                        2,
                        root,
                        0,
                        new ImmutableMethodReference(
                                HELPER, "refresh", List.of("Landroid/view/View;", "I"), "V")));
    }

    static int managerEnd(Method m) {
        int at = 0;

        for (var i : m.getImplementation().getInstructions()) {
            at += i.getCodeUnits();

            if (i instanceof ReferenceInstruction r
                    && r.getReference()
                            .toString()
                            .equals(
                                    OWNER
                                            + "->mManager:Lcom/ahos/common/binder/RemoteServiceManager;")) {
                return at;
            }
        }

        throw new IllegalStateException("manager load required");
    }

    static Method rewrite(Method m, Target t, boolean inverse) {
        var b = new MutableMethodImplementation(m.getImplementation());

        if (t.kind <= 1) {
            rewriteManagerGuard(m, t, b, inverse);
        } else if (t.kind == 2) {
            rewriteUpdateGuard(b, inverse);
        } else {
            rewriteAvailabilityGuard(t, b, inverse);
        }

        return PassionServiceForegroundGuard.copy(m, b);
    }

    private static void rewriteManagerGuard(
            Method m, Target t, MutableMethodImplementation b, boolean inverse) {
        int address = managerEnd(m), index = PassionServiceForegroundGuard.index(b, address);

        if (inverse) {
            int count = t.kind == 0 || !m.getReturnType().equals("V") ? 3 : 2;

            for (int i = 0; i < count; i++) {
                b.removeInstruction(index);
            }
        } else {
            var next = b.newLabelForAddress(address);
            int manager =
                    ((OneRegisterInstruction) b.getInstructions().get(index - 1)).getRegisterA();
            b.addInstruction(index++, new BuilderInstruction21t(Opcode.IF_NEZ, manager, next));

            if (t.kind == 0 || !m.getReturnType().equals("V")) {
                b.addInstruction(
                        index++,
                        new BuilderInstruction11n(Opcode.CONST_4, manager, t.kind == 0 ? -1 : 0));
            }

            b.addInstruction(
                    index,
                    m.getReturnType().equals("V")
                            ? new BuilderInstruction10x(Opcode.RETURN_VOID)
                            : new BuilderInstruction11x(Opcode.RETURN, manager));
        }
    }

    private static void rewriteUpdateGuard(MutableMethodImplementation b, boolean inverse) {
        if (inverse) {
            for (int i = 0; i < 3; i++) {
                b.removeInstruction(0);
            }
        } else {
            var end = b.newLabelForAddress(16);
            b.addInstruction(
                    0,
                    field(
                            0,
                            3,
                            OWNER,
                            "mManager",
                            "Lcom/ahos/common/binder/RemoteServiceManager;"));
            b.addInstruction(1, new BuilderInstruction21t(Opcode.IF_EQZ, 0, end));
            b.addInstruction(2, new BuilderInstruction10x(Opcode.NOP));
        }
    }

    private static void rewriteAvailabilityGuard(
            Target t, MutableMethodImplementation b, boolean inverse) {
        int address = t.kind == 3 ? 108 : 0,
                skip = t.kind == 3 ? 182 : t.kind == 4 ? 19 : 44,
                index = PassionServiceForegroundGuard.index(b, address);

        if (inverse) {
            for (int i = 0; i < 9; i++) {
                b.removeInstruction(index);
            }
        } else {
            var end = b.newLabelForAddress(skip);
            var prefix = availability(t.kind == 3 ? 7 : 2, t.kind == 3 ? 3 : 1);

            for (var i : prefix) {
                b.addInstruction(index++, i);
            }

            b.addInstruction(index, new BuilderInstruction21t(Opcode.IF_LTZ, 0, end));
        }
    }

    static Map<String, ClassDef> classes(byte[] bytes) {
        return PassionServiceForegroundGuard.classes(bytes);
    }

    static void validateMethod(Method m, Target t) throws Exception {
        require(
                m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == t.registers);
    }

    static Map<String, ClassDef> transform(Map<String, ClassDef> input, boolean inverse)
            throws Exception {
        var out = new TreeMap<String, ClassDef>();
        int count = 0;

        for (var c : input.values()) {
            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();
            int groupIndex = 0;

            for (var group : List.of(c.getDirectMethods(), c.getVirtualMethods())) {
                var destination = groupIndex++ == 0 ? direct : virtual;

                for (var m : group) {
                    var t = target(m);
                    Method next = m;

                    if (t != null) {
                        if (!inverse) {
                            validateMethod(m, t);
                        }

                        next = rewrite(m, t, inverse);

                        if (inverse) {
                            validateMethod(next, t);
                        }

                        count++;
                    }

                    destination.add(next);
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

        require(count == (input.containsKey(OWNER) ? 8 : 3));

        return out;
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        require(
                PROFILE.equals(profile)
                        && api == 30
                        && PIN.equals(FilesSupport.hash(original))
                        && dex != null);
        transform(classes(dex), false);
    }

    /** The Bluetooth step prepares classes2.dex, the audio and volume steps classes3.dex. */
    static void validateOriginalChain(
            String profile, int api, Path original, String entry, byte[] raw) throws Exception {
        byte[] prepared;

        if (entry.equals("classes2.dex")) {
            prepared = PassionVehicleBluetoothGuard.apply(profile, api, original, raw);
        } else {
            byte[] audio = PassionVehicleAudioGuard.apply(profile, api, original, raw);
            prepared = PassionBluetoothVolumeGuard.apply(profile, api, original, audio);
        }

        validate(profile, api, original, prepared);
    }

    static void validateAfter(Method before, Method after) throws Exception {
        var t = target(before);

        require(t != null);
        validateMethod(before, t);
        require(
                FrameworkDexMaterializer.semantic(rewrite(before, t, false))
                        .equals(FrameworkDexMaterializer.semantic(after)));
        require(
                FrameworkDexMaterializer.semantic(rewrite(after, t, true))
                        .equals(FrameworkDexMaterializer.semantic(before)));
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        validate(profile, api, original, dex);
        var before = classes(dex);
        var expected = transform(before, false);
        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), expected.values()));
        var actual = classes(output);
        require(actual.keySet().equals(before.keySet()));

        for (var name : expected.keySet()) {
            require(
                    FrameworkDexMaterializer.semantic(expected.get(name))
                            .equals(FrameworkDexMaterializer.semantic(actual.get(name))));
        }

        var restored = transform(actual, true);

        for (var name : before.keySet()) {
            require(
                    FrameworkDexMaterializer.semantic(before.get(name))
                            .equals(FrameworkDexMaterializer.semantic(restored.get(name))));
        }

        return output;
    }
}
