package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.reference.ImmutableFieldReference;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Guard the reproduced optional automotive manager without inventing mute state. */
final class PassionLauncherAudioGuard {
    static final String PROFILE = PassionServiceForegroundGuard.PROFILE,
            NAME = "passion-api30-launcher-audio",
            PIN = "4032f14c0fc49fd52514737309d2af67c9d4ff9340dd534874f46cbe2e748c00";
    static final String OWNER = "Lcom/voyah/os/carsystemui/viewmodel/ExpandStatusViewModel;",
            BAR = "Lcom/voyah/os/carsystemui/view/ExpandStatusBar;",
            HELPER = "Lcom/voyemu/window/PassionOneKeyMute;";

    record Target(String method, int registers, int kind) {}

    static final List<Target> TARGETS =
            List.of(
                    new Target("initMuteSwith", 6, 0),
                    new Target("init", 11, 1),
                    new Target("setExpandStatusSub", 3, 2));

    static void require(boolean b) {
        Failure.require(
                b,
                "PASSION_LAUNCHER_AUDIO_CONTRACT",
                "Exact optional Launcher audio contract required",
                3);
    }

    static Target target(Method m) {
        if (m.getDefiningClass().equals(OWNER)) {
            for (var t : TARGETS) {
                if (m.getName().equals(t.method)) {
                    return t;
                }
            }
        }

        return null;
    }

    static BuilderInstruction22c field(int dest, int owner, String name, String type) {
        return new BuilderInstruction22c(
                Opcode.IGET_OBJECT, dest, owner, new ImmutableFieldReference(OWNER, name, type));
    }

    static BuilderInstruction35c refresh(int view, int manager) {
        return new BuilderInstruction35c(
                Opcode.INVOKE_STATIC,
                2,
                view,
                manager,
                0,
                0,
                0,
                new ImmutableMethodReference(
                        HELPER,
                        "refresh",
                        List.of("Landroid/view/View;", "Ljava/lang/Object;"),
                        "V"));
    }

    static Method rewrite(Method m, Target t, boolean inverse) {
        var b = new MutableMethodImplementation(m.getImplementation());
        int prefix = t.kind == 0 ? 5 : 2;

        if (inverse) {
            for (int i = 0; i < prefix; i++) {
                b.removeInstruction(0);
            }

            if (t.kind == 0) {
                b.removeInstruction(PassionServiceForegroundGuard.index(b, 20));
            }
        } else {
            if (t.kind == 0) {
                int index = PassionServiceForegroundGuard.index(b, 20);
                var end = b.newLabelForAddress(56);
                b.addInstruction(index, new BuilderInstruction21t(Opcode.IF_EQZ, 2, end));
                b.addInstruction(
                        0, field(1, 5, "mCarAudioManager", "Landroid/car/media/CarAudioManager;"));
                b.addInstruction(1, field(0, 5, "mExpandStatus", BAR));
                b.addInstruction(2, refresh(0, 1));
                b.addInstruction(3, field(0, 5, "mExpandStatusSub", BAR));
                b.addInstruction(4, refresh(0, 1));
            } else {
                int owner = t.kind == 1 ? 8 : 1, view = t.kind == 1 ? 9 : 2;
                b.addInstruction(
                        0,
                        field(0, owner, "mCarAudioManager", "Landroid/car/media/CarAudioManager;"));
                b.addInstruction(1, refresh(view, 0));
            }
        }

        return PassionServiceForegroundGuard.copy(m, b);
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

        require(count == 3);

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

    static void validateOriginalChain(String profile, int api, Path original, byte[] raw)
            throws Exception {
        byte[] sidebar = PassionSidebarGuard.apply(profile, api, original, raw);
        byte[] ear = PassionBluetoothVolumeGuard.apply(profile, api, original, sidebar);

        validate(profile, api, original, ear);
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
