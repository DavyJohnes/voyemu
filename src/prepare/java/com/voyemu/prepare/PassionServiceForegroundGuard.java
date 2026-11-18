package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableStringReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/** Silence only three pinned service-status notifications; media playback is unchanged. */
final class PassionServiceForegroundGuard {
    static final String PROFILE = "passion-11-russian-20260403-2.1.4.80",
            NAME = "passion-api30-service-foreground";
    static final String VIDEO = "11bd7f83630949a997836753a65e4782f3235cbbe83db037c7b47e25ddd21c5c",
            CAMERA = "e0e57f7cd9099136541c3ed35e1042fba1762a13640bea7f6a2ac3415f426bf0";
    static final String SOUND =
            "Landroid/app/NotificationChannel;->setSound(Landroid/net/Uri;Landroid/media/AudioAttributes;)V";

    record Target(
            String owner,
            String method,
            int registers,
            int idOffset,
            int idRegister,
            String oldId,
            String newId,
            int publication,
            int channel,
            int zero) {}

    static final List<Target> VIDEO_TARGETS =
            List.of(
                    new Target(
                            "Lcom/voyah/videobridge/service/VideoBridgeService;",
                            "onCreate",
                            6,
                            27,
                            0,
                            "CHANNEL_ID",
                            "voyemu.passion.video.bridge.foreground.v1",
                            56,
                            2,
                            4));
    static final List<Target> CAMERA_TARGETS =
            List.of(
                    new Target(
                            "Lcom/voyah/camera/service/CameraAppService;",
                            "start",
                            6,
                            14,
                            0,
                            "ID_CAMERA_APP",
                            "voyemu.passion.camera.app.foreground.v1",
                            43,
                            2,
                            4),
                    new Target(
                            "Lcom/voyah/camera/service/CameraLensService;",
                            "start",
                            5,
                            7,
                            1,
                            "CHANNEL_ID",
                            "voyemu.passion.camera.lens.foreground.v1",
                            34,
                            0,
                            3));
    static final Set<String> PINS = Set.of(VIDEO, CAMERA);

    private static void require(boolean value) {
        Failure.require(
                value,
                "PASSION_SERVICE_FOREGROUND_CONTRACT",
                "Exact Passion service foreground channel contract required",
                3);
    }

    static List<Target> targets(String pin) {
        require(PINS.contains(pin));

        return pin.equals(VIDEO) ? VIDEO_TARGETS : CAMERA_TARGETS;
    }

    static Target target(Method method, List<Target> targets) {
        for (var t : targets) {
            if (method.getDefiningClass().equals(t.owner)
                    && method.getName().equals(t.method)
                    && method.getParameterTypes().isEmpty()
                    && method.getReturnType().equals("V")) {
                return t;
            }
        }

        return null;
    }

    static int index(MutableMethodImplementation body, int address) {
        int offset = 0;

        for (int i = 0; i < body.getInstructions().size(); i++) {
            if (offset == address) {
                return i;
            }

            offset += body.getInstructions().get(i).getCodeUnits();
        }

        throw new Failure(
                "PASSION_SERVICE_FOREGROUND_CONTRACT", "Exact instruction boundary required", 3);
    }

    static Method copy(Method m, MutableMethodImplementation body) {
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

    static void validateOriginal(Method m, Target t) throws Exception {
        require(
                t.equals(target(m, List.of(t)))
                        && m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == t.registers
                        && m.getImplementation().getTryBlocks().isEmpty());
    }

    static void validateAfter(Method m, Target t) throws Exception {
        var b = new MutableMethodImplementation(m.getImplementation());
        var id = b.getInstructions().get(index(b, t.idOffset));

        require(
                id.getOpcode() == Opcode.CONST_STRING
                        && id instanceof ReferenceInstruction referenceInstruction
                        && id instanceof OneRegisterInstruction oneRegisterInstruction
                        && oneRegisterInstruction.getRegisterA() == t.idRegister
                        && referenceInstruction.getReference().toString().equals(t.newId));
        var call = b.getInstructions().get(index(b, t.publication));
        require(
                call.getOpcode() == Opcode.INVOKE_VIRTUAL
                        && call instanceof ReferenceInstruction referenceInstruction
                        && call instanceof FiveRegisterInstruction
                        && referenceInstruction.getReference().toString().equals(SOUND));
        var r = (FiveRegisterInstruction) call;
        require(
                r.getRegisterCount() == 3
                        && r.getRegisterC() == t.channel
                        && r.getRegisterD() == t.zero
                        && r.getRegisterE() == t.zero);
        validateOriginal(rewrite(m, t, true), t);
    }

    static Method rewrite(Method m, Target t, boolean inverse) {
        var b = new MutableMethodImplementation(m.getImplementation());

        if (inverse) {
            b.removeInstruction(index(b, t.publication));
        }

        b.replaceInstruction(
                index(b, t.idOffset),
                new BuilderInstruction21c(
                        Opcode.CONST_STRING,
                        t.idRegister,
                        new ImmutableStringReference(inverse ? t.oldId : t.newId)));

        if (!inverse) {
            b.addInstruction(
                    index(b, t.publication),
                    new BuilderInstruction35c(
                            Opcode.INVOKE_VIRTUAL,
                            3,
                            t.channel,
                            t.zero,
                            t.zero,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    "Landroid/app/NotificationChannel;",
                                    "setSound",
                                    List.of("Landroid/net/Uri;", "Landroid/media/AudioAttributes;"),
                                    "V")));
        }

        return copy(m, b);
    }

    static Map<String, ClassDef> classes(byte[] bytes) {
        var out = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), bytes).getClasses()) {
            require(out.put(c.getType(), c) == null);
        }

        return out;
    }

    static Map<String, ClassDef> transform(
            Map<String, ClassDef> input, List<Target> targets, boolean inverse) throws Exception {
        var out = new TreeMap<String, ClassDef>();
        int changes = 0;

        for (var c : input.values()) {
            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();
            int groupIndex = 0;

            for (var group : List.of(c.getDirectMethods(), c.getVirtualMethods())) {
                var destination = groupIndex++ == 0 ? direct : virtual;

                for (var m : group) {
                    var t = target(m, targets);
                    Method next = m;

                    if (t != null) {
                        if (inverse) {
                            validateAfter(m, t);
                        } else {
                            validateOriginal(m, t);
                        }

                        next = rewrite(m, t, inverse);
                        changes++;
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

        require(changes == targets.size());

        return out;
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        require(PROFILE.equals(profile) && api == 30 && dex != null);
        String pin = FilesSupport.hash(original);
        require(PINS.contains(pin));

        for (var t : targets(pin)) {
            int count = 0;

            for (var c : classes(dex).values()) {
                for (var m : c.getMethods()) {
                    if (target(m, List.of(t)) != null) {
                        validateOriginal(m, t);
                        count++;
                    }
                }
            }

            require(count == 1);
        }
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        validate(profile, api, original, dex);
        String pin = FilesSupport.hash(original);
        var before = classes(dex);
        var expected = transform(before, targets(pin), false);
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

        var restored = transform(actual, targets(pin), true);

        for (var name : before.keySet()) {
            require(
                    FrameworkDexMaterializer.semantic(before.get(name))
                            .equals(FrameworkDexMaterializer.semantic(restored.get(name))));
        }

        require(FilesSupport.hash(original).equals(pin));

        return output;
    }
}
