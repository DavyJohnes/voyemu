package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11n;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableFieldReference;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableTypeReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/** Preserve supported SDK streams; represent the absent OEM stream separately. */
final class PassionBluetoothVolumeGuard {
    static final String NAME = "passion-api30-bluetooth-volume";
    static final String HELPER = "Lcom/voyemu/window/PassionBluetoothVolume;";

    record Target(String owner, String method, int registers, int calls) {}

    static final List<Target> TARGETS =
            List.of(
                    new Target(
                            "Lcom/voyah/os/carsystemui/utils/VolumeChangeObserver;",
                            "getMaxEarVolume",
                            2,
                            1),
                    new Target(
                            "Lcom/voyah/os/carsystemui/utils/VolumeChangeObserver;",
                            "getStreamMusicVolume",
                            2,
                            1),
                    new Target(
                            "Lcom/voyah/os/carsystemui/view/ExpandStatusBar$2;",
                            "onProgressChanged",
                            7,
                            2),
                    new Target(
                            "Lcom/voyah/os/carsystemui/viewmodel/ExpandStatusViewModel;",
                            "onEarPhoneConnect",
                            5,
                            0),
                    new Target(
                            "Lcom/voyah/os/carsystemui/viewmodel/ExpandStatusViewModel;",
                            "setExpandStatusSub",
                            3,
                            0),
                    new Target(
                            "Lcom/voyah/car/vm/common/sound/SoundValueViewModelN;",
                            "getCurrentMediaVolume",
                            4,
                            2),
                    new Target(
                            "Lcom/voyah/car/vm/common/sound/SoundValueViewModelN;",
                            "setMediaProgress",
                            8,
                            3),
                    new Target(
                            "Lcom/voyah/car/vm/common/sound/SoundValueViewModelN;",
                            "setMediaVolume",
                            6,
                            4),
                    new Target(
                            "Lcom/voyah/baselibrary/soundsetting/BtHeadSetHelper;",
                            "resetMediaVol",
                            5,
                            4),
                    new Target(
                            "Lcom/voyah/baselibrary/utils/AudioMngHelper;",
                            "getSystemBtCurrentVolume",
                            2,
                            1),
                    new Target(
                            "Lcom/voyah/baselibrary/utils/AudioMngHelper;",
                            "getSystemBtMaxVolume",
                            2,
                            1),
                    new Target(
                            "Lcom/voyah/baselibrary/utils/AudioMngHelper;",
                            "setVoice100BtHeadSet",
                            6,
                            1));

    private static void require(boolean value) {
        Failure.require(
                value,
                "PASSION_BLUETOOTH_VOLUME_CONTRACT",
                "Exact Passion Bluetooth volume contract required",
                3);
    }

    static Target target(Method m) {
        for (var t : TARGETS) {
            if (m.getDefiningClass().equals(t.owner) && m.getName().equals(t.method)) {
                return t;
            }
        }

        return null;
    }

    static Method rewrite(Method m, Target t, boolean inverse) throws Exception {
        var body = new MutableMethodImplementation(m.getImplementation());
        int changed = routeAudioManager(body, inverse);

        require(changed == t.calls);

        if (t.method.equals("onEarPhoneConnect")) {
            routeSecondaryMax(body, inverse);
        }

        lifecycle(body, t, inverse);

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                body);
    }

    private static int routeAudioManager(MutableMethodImplementation body, boolean inverse) {
        int changed = 0;

        for (int i = 0; i < body.getInstructions().size(); i++) {
            var instruction = body.getInstructions().get(i);

            if (!(instruction instanceof ReferenceInstruction r)
                    || !(r.getReference()
                            instanceof org.jf.dexlib2.iface.reference.MethodReference ref)) {
                continue;
            }

            if (!ref.getDefiningClass().equals(inverse ? HELPER : "Landroid/media/AudioManager;")
                    || !Set.of(
                                    "getStreamVolume",
                                    "getStreamMaxVolume",
                                    "getStreamMinVolume",
                                    "setStreamVolume")
                            .contains(ref.getName())) {
                continue;
            }

            require(
                    instruction instanceof FiveRegisterInstruction
                            && instruction.getOpcode()
                                    == (inverse ? Opcode.INVOKE_STATIC : Opcode.INVOKE_VIRTUAL));
            var parameters = new ArrayList<String>();

            for (var p : ref.getParameterTypes()) {
                parameters.add(p.toString());
            }

            if (inverse) {
                parameters.remove(0);
            } else {
                parameters.add(0, "Landroid/media/AudioManager;");
            }

            var replacement =
                    new ImmutableMethodReference(
                            inverse ? "Landroid/media/AudioManager;" : HELPER,
                            ref.getName(),
                            parameters,
                            ref.getReturnType());
            var registers = (FiveRegisterInstruction) instruction;
            body.replaceInstruction(
                    i,
                    new BuilderInstruction35c(
                            inverse ? Opcode.INVOKE_VIRTUAL : Opcode.INVOKE_STATIC,
                            registers.getRegisterCount(),
                            registers.getRegisterC(),
                            registers.getRegisterD(),
                            registers.getRegisterE(),
                            registers.getRegisterF(),
                            registers.getRegisterG(),
                            replacement));
            changed++;
        }

        return changed;
    }

    private static void routeSecondaryMax(MutableMethodImplementation body, boolean inverse) {
        for (int i = 0; i < body.getInstructions().size(); i++) {
            var in = body.getInstructions().get(i);

            if (in instanceof ReferenceInstruction ref
                    && ref.getReference()
                            .toString()
                            .equals(
                                    inverse
                                            ? HELPER
                                                    + "->setSecondaryMax(Landroid/widget/SeekBar;I)V"
                                            : "Landroid/widget/SeekBar;->setMax(I)V")) {
                var r = (FiveRegisterInstruction) in;
                body.replaceInstruction(
                        i,
                        new BuilderInstruction35c(
                                inverse ? Opcode.INVOKE_VIRTUAL : Opcode.INVOKE_STATIC,
                                2,
                                r.getRegisterC(),
                                r.getRegisterD(),
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        inverse ? "Landroid/widget/SeekBar;" : HELPER,
                                        inverse ? "setMax" : "setSecondaryMax",
                                        inverse
                                                ? List.of("I")
                                                : List.of("Landroid/widget/SeekBar;", "I"),
                                        "V")));
            }
        }
    }

    static BuilderInstruction35c call(
            String name, List<String> parameters, String result, int count, int a, int b) {
        return new BuilderInstruction35c(
                Opcode.INVOKE_STATIC,
                count,
                a,
                b,
                0,
                0,
                0,
                new ImmutableMethodReference(HELPER, name, parameters, result));
    }

    static void lifecycle(MutableMethodImplementation b, Target t, boolean inverse) {
        if (t.method.equals("setVoice100BtHeadSet")) {
            int at = 2;

            if (inverse) {
                for (int i = 0; i < 3; i++) {
                    b.removeInstruction(at);
                }
            } else {
                var next = b.newLabelForIndex(at);
                b.addInstruction(at++, new BuilderInstruction21t(Opcode.IF_GEZ, 0, next));
                b.addInstruction(at++, new BuilderInstruction11n(Opcode.CONST_4, 0, -1));
                b.addInstruction(at, new BuilderInstruction11x(Opcode.RETURN, 0));
            }
        }

        if (!t.owner.equals("Lcom/voyah/os/carsystemui/viewmodel/ExpandStatusViewModel;")) {
            return;
        }

        // These locations precede the OEM reuse of p0; only dead v0/v1 are borrowed.
        if (t.method.equals("onEarPhoneConnect")) {
            int at = PassionServiceForegroundGuard.index(b, 36);

            if (inverse) {
                for (int i = 0; i < 3; i++) {
                    b.removeInstruction(at);
                }
            } else {
                b.addInstruction(
                        at++,
                        new BuilderInstruction22c(
                                Opcode.IGET_OBJECT,
                                0,
                                3,
                                new ImmutableFieldReference(
                                        t.owner,
                                        "mExpandStatusSub",
                                        "Lcom/voyah/os/carsystemui/view/ExpandStatusBar;")));
                b.addInstruction(
                        at++,
                        new BuilderInstruction22c(
                                Opcode.IGET_OBJECT,
                                0,
                                0,
                                new ImmutableFieldReference(
                                        "Lcom/voyah/os/carsystemui/view/ExpandStatusBar;",
                                        "mMediaVolumeBar",
                                        "Landroid/widget/SeekBar;")));
                b.addInstruction(
                        at,
                        call(
                                "refreshSecondary",
                                List.of("Landroid/widget/SeekBar;", "Z"),
                                "V",
                                2,
                                0,
                                4));
            }
        }

        if (t.method.equals("setExpandStatusSub")) {
            int at = 1;

            if (inverse) {
                for (int i = 0; i < 12; i++) {
                    b.removeInstruction(at);
                }
            } else {
                var end = b.newLabelForIndex(at);
                b.addInstruction(
                        at++,
                        new BuilderInstruction22c(
                                Opcode.IGET_OBJECT,
                                0,
                                1,
                                new ImmutableFieldReference(
                                        t.owner,
                                        "dataInfo",
                                        "Landroidx/databinding/ObservableField;")));
                b.addInstruction(at++, new BuilderInstruction21t(Opcode.IF_EQZ, 0, end));
                b.addInstruction(
                        at++,
                        new BuilderInstruction35c(
                                Opcode.INVOKE_VIRTUAL,
                                1,
                                0,
                                0,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        "Landroidx/databinding/ObservableField;",
                                        "get",
                                        List.of(),
                                        "Ljava/lang/Object;")));
                b.addInstruction(at++, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0));
                b.addInstruction(at++, new BuilderInstruction21t(Opcode.IF_EQZ, 0, end));
                b.addInstruction(
                        at++,
                        new BuilderInstruction21c(
                                Opcode.CHECK_CAST,
                                0,
                                new ImmutableTypeReference(
                                        "Lcom/voyah/os/carsystemui/bean/ExpandDataInfo;")));
                b.addInstruction(
                        at++,
                        new BuilderInstruction35c(
                                Opcode.INVOKE_VIRTUAL,
                                1,
                                0,
                                0,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        "Lcom/voyah/os/carsystemui/bean/ExpandDataInfo;",
                                        "isEarPhoneConnect",
                                        List.of(),
                                        "Z")));
                b.addInstruction(at++, new BuilderInstruction11x(Opcode.MOVE_RESULT, 0));
                b.addInstruction(at++, new BuilderInstruction21t(Opcode.IF_EQZ, 2, end));
                b.addInstruction(
                        at++,
                        new BuilderInstruction22c(
                                Opcode.IGET_OBJECT,
                                2,
                                2,
                                new ImmutableFieldReference(
                                        "Lcom/voyah/os/carsystemui/view/ExpandStatusBar;",
                                        "mMediaVolumeBar",
                                        "Landroid/widget/SeekBar;")));
                b.addInstruction(
                        at++,
                        call(
                                "refreshSecondary",
                                List.of("Landroid/widget/SeekBar;", "Z"),
                                "V",
                                2,
                                2,
                                0));
                b.addInstruction(
                        at,
                        new BuilderInstruction22c(
                                Opcode.IGET_OBJECT,
                                2,
                                1,
                                new ImmutableFieldReference(
                                        t.owner,
                                        "mExpandStatusSub",
                                        "Lcom/voyah/os/carsystemui/view/ExpandStatusBar;")));
            }
        }
    }

    static final Set<String> PINS =
            Set.of(
                    "4032f14c0fc49fd52514737309d2af67c9d4ff9340dd534874f46cbe2e748c00",
                    "a5fb5daed4a295a568daf0390858a0bd00782b1384e66b13a96f66d43ad43e60",
                    "11bd7f83630949a997836753a65e4782f3235cbbe83db037c7b47e25ddd21c5c");

    static Map<String, ClassDef> classes(byte[] bytes) {
        return PassionServiceForegroundGuard.classes(bytes);
    }

    static Map<String, ClassDef> transform(Map<String, ClassDef> input, boolean inverse)
            throws Exception {
        var result = new TreeMap<String, ClassDef>();
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
                        require(
                                m.getImplementation() != null
                                        && m.getImplementation().getRegisterCount() == t.registers);
                        next = rewrite(m, t, inverse);
                        count++;
                    }

                    destination.add(next);
                }
            }

            result.put(
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

        require(count > 0);

        return result;
    }

    static void validateAfter(Method before, Method after) throws Exception {
        var t = target(before);

        require(t != null);
        require(
                FrameworkDexMaterializer.semantic(rewrite(before, t, false))
                        .equals(FrameworkDexMaterializer.semantic(after)));
        require(
                FrameworkDexMaterializer.semantic(rewrite(after, t, true))
                        .equals(FrameworkDexMaterializer.semantic(before)));
    }

    static void validateOriginalChain(String profile, int api, Path original, byte[] raw)
            throws Exception {
        require(PassionServiceForegroundGuard.PROFILE.equals(profile) && api == 30 && raw != null);
        String apk = FilesSupport.hash(original);
        require(PINS.contains(apk));
        byte[] prepared =
                apk.equals(PassionServiceForegroundGuard.VIDEO)
                        ? PassionServiceForegroundGuard.apply(profile, api, original, raw)
                        : apk.equals(PassionVehicleAudioGuard.PIN)
                                ? PassionVehicleAudioGuard.apply(profile, api, original, raw)
                                : PassionSidebarGuard.apply(profile, api, original, raw);
        validate(profile, api, original, prepared);
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        require(PassionServiceForegroundGuard.PROFILE.equals(profile) && api == 30 && dex != null);
        String apk = FilesSupport.hash(original);
        require(PINS.contains(apk));

        for (var c : classes(dex).values()) {
            for (var m : c.getMethods()) {
                var t = target(m);

                if (t != null) {
                    require(
                            m.getImplementation() != null
                                    && m.getImplementation().getRegisterCount() == t.registers);
                }
            }
        }
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        validate(profile, api, original, dex);
        String apk = FilesSupport.hash(original);
        var before = classes(dex);
        var expected = transform(before, false);
        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), expected.values()));
        var actual = classes(output);
        require(before.keySet().equals(actual.keySet()));

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

        require(FilesSupport.hash(original).equals(apk));

        return output;
    }
}
