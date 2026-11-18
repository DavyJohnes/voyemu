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
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.MethodImplementation;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableTypeReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/** Exact OEM Camera-only adaptation to an actual virtual BACK camera. */
final class CameraVirtualGuard {
    record Variant(String pin) {}

    static final Map<String, Variant> VARIANTS =
            Map.of(
                    "free-11-chinese-20250710-6.11.1",
                    new Variant("17e34bdc4d4bdfd8a42cc99feb3bc7165f0c1ad90bc131127b1618e45b672507"),
                    "free-11-chinese-20260731-7.3.1",
                    new Variant(
                            "c680a80c0bfb13911979693c7dc46e8f75df69b361fc6f17a3a94aef28fbaabf"));

    static List<String> pins(String profile) {
        var variant = VARIANTS.get(profile);

        return variant == null ? List.of() : List.of(variant.pin());
    }

    static final String NAME = "api30-camera-virtual",
            HELPER = "Lcom/voyemu/camera/BackCameraProbe;";
    static final String
            HELPER_PATH = "api30-camera-virtual/src/com/voyemu/camera/BackCameraProbe.java",
            HELPER_ROOT = "api30-camera-virtual/src";
    static final String SUPPORT = "Lcom/qinggan/camera/common/SupportUtils;",
            ACTIVITY = "Lcom/qinggan/camera/activity/RecordVideoActivity;",
            FILTER = "Lcom/qinggan/camera/AppMain$CameraConfigFilter;";
    static final Set<String> TARGETS =
            Set.of(
                    SUPPORT + "->isSupportDvr",
                    ACTIVITY + "->isDVRCameraError",
                    ACTIVITY + "->initIntent",
                    ACTIVITY + "->openCamera$lambda-14",
                    FILTER + "->filter");

    static void need(boolean b) {
        Failure.require(
                b, "CAMERA_VIRTUAL", "Exact Free A11 7.3.1 Camera virtual boundary required", 3);
    }

    static Map<String, ClassDef> classes(byte[] raw) {
        var out = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), raw).getClasses()) {
            need(out.put(c.getType(), c) == null);
        }

        return out;
    }

    static String key(Method m) {
        return m.getDefiningClass() + "->" + m.getName();
    }

    static void validate(String profile, int api, String abi, Path apk, byte[] raw)
            throws Exception {
        need(
                VARIANTS.containsKey(profile)
                        && api == 30
                        && "arm64-v8a".equals(abi)
                        && VARIANTS.get(profile).pin().equals(FilesSupport.hash(apk))
                        && raw != null);
        var seen = new HashSet<String>();

        for (var c : classes(raw).values()) {
            for (var m : c.getMethods()) {
                String k = key(m);

                if (TARGETS.contains(k)) {
                    need(seen.add(k));
                }
            }
        }

        need(seen.equals(TARGETS));
    }

    static ImmutableMethod copy(Method m, MethodImplementation code) {
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

    static Method adapt(Method m) {
        String key = key(m);

        if (!TARGETS.contains(key)) {
            return m;
        }

        var b = new MutableMethodImplementation(m.getImplementation());

        if (key.equals(SUPPORT + "->isSupportDvr") || key.equals(ACTIVITY + "->isDVRCameraError")) {
            adaptSupportCheck(b, key);
        } else if (key.equals(FILTER + "->filter")) {
            adaptFilter(m, b);
        } else if (key.equals(ACTIVITY + "->initIntent")) {
            adaptInitIntent(b);
        } else {
            adaptCameraSelection(b);
        }

        return copy(m, b);
    }

    static void adaptSupportCheck(MutableMethodImplementation b, String key) {
        var original = b.newLabelForIndex(0);

        b.addInstruction(
                0,
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        0,
                        0,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(HELPER, "isSupported", List.of(), "Z")));
        b.addInstruction(1, new BuilderInstruction11x(Opcode.MOVE_RESULT, 0));
        b.addInstruction(2, new BuilderInstruction21t(Opcode.IF_EQZ, 0, original));
        b.addInstruction(
                3, new BuilderInstruction11n(Opcode.CONST_4, 0, key.startsWith(SUPPORT) ? 1 : 0));
        b.addInstruction(4, new BuilderInstruction11x(Opcode.RETURN, 0));
    }

    static void adaptFilter(Method m, MutableMethodImplementation b) {
        int n = 0;

        for (int i = 0; i < b.getInstructions().size(); i++) {
            if (b.getInstructions().get(i).getOpcode() == Opcode.RETURN_OBJECT) {
                var op = (OneRegisterInstruction) b.getInstructions().get(i);
                need(op.getRegisterA() == 0 && m.getImplementation().getRegisterCount() == 7);
                b.replaceInstruction(
                        i,
                        new BuilderInstruction35c(
                                Opcode.INVOKE_STATIC,
                                2,
                                6,
                                0,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        HELPER,
                                        "augment",
                                        List.of("Ljava/util/List;", "Ljava/util/List;"),
                                        "Ljava/util/List;")));
                b.addInstruction(i + 1, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0));
                b.addInstruction(i + 2, new BuilderInstruction11x(Opcode.RETURN_OBJECT, 0));
                i += 2;
                n++;
            }
        }

        need(n == 1);
    }

    static void adaptInitIntent(MutableMethodImplementation b) {
        int at = initialTypeCall(b);

        b.addInstruction(
                at,
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        1,
                        5,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(HELPER, "defaultType", List.of("I"), "I")));
        b.addInstruction(at + 1, new BuilderInstruction11x(Opcode.MOVE_RESULT, 5));
        b.addInstruction(at + 3, new BuilderInstruction11n(Opcode.CONST_4, 5, 0));
        int entry = intentTypeStore(b);
        var store = b.getInstructions().get(entry);
        need(
                store.getOpcode() == Opcode.IPUT
                        && store instanceof TwoRegisterInstruction
                        && store instanceof ReferenceInstruction);
        var sr = (TwoRegisterInstruction) store;
        need(sr.getRegisterA() == 6);
        var field = ((ReferenceInstruction) store).getReference();
        b.replaceInstruction(
                entry,
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        1,
                        6,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(HELPER, "localEntry", List.of("I"), "I")));
        b.addInstruction(entry + 1, new BuilderInstruction11x(Opcode.MOVE_RESULT, 6));
        b.addInstruction(
                entry + 2,
                new BuilderInstruction22c(
                        Opcode.IPUT, sr.getRegisterA(), sr.getRegisterB(), field));
    }

    static int initialTypeCall(MutableMethodImplementation b) {
        boolean seen = false;
        int at = -1;

        for (int i = 0; i < b.getInstructions().size(); i++) {
            var op = b.getInstructions().get(i);

            if (op instanceof ReferenceInstruction r) {
                String ref = r.getReference().toString();

                if (ref.contains("cameraType is init, set self_front")) {
                    seen = true;
                } else if (seen
                        && ref.equals(
                                "Lcom/qinggan/camera/common/CameraSetting;->setCameraType(I)V")) {
                    need(
                            at < 0
                                    && op
                                            instanceof
                                            org.jf.dexlib2.iface.instruction.formats.Instruction35c
                                                    instruction35c
                                    && instruction35c.getRegisterD() == 5);
                    at = i;
                    break;
                }
            }
        }

        need(at >= 0);

        return at;
    }

    static int intentTypeStore(MutableMethodImplementation b) {
        int entry = -1;

        for (int i = 0; i + 3 < b.getInstructions().size(); i++) {
            var x = b.getInstructions().get(i);

            if (x instanceof ReferenceInstruction r
                    && r.getReference().toString().equals("mInType")) {
                var call = b.getInstructions().get(i + 1);
                var result = b.getInstructions().get(i + 2);
                var field = b.getInstructions().get(i + 3);

                if (call instanceof ReferenceInstruction cr
                        && cr.getReference()
                                .toString()
                                .equals(
                                        "Landroid/content/Intent;->getIntExtra(Ljava/lang/String;I)I")
                        && result.getOpcode() == Opcode.MOVE_RESULT
                        && ((OneRegisterInstruction) result).getRegisterA() == 6
                        && field instanceof ReferenceInstruction fr
                        && fr.getReference()
                                .toString()
                                .equals(
                                        "Lcom/qinggan/camera/activity/RecordVideoActivity;->mInType:I")) {
                    need(entry < 0);
                    entry = i + 3;
                }
            }
        }

        need(entry >= 0);

        return entry;
    }

    static void adaptCameraSelection(MutableMethodImplementation b) {
        int selector = -1, quality = -1;

        for (int i = 0; i < b.getInstructions().size(); i++) {
            var op = b.getInstructions().get(i);

            if (op instanceof ReferenceInstruction r) {
                String ref = r.getReference().toString();

                if (ref.equals(
                        "Lcom/qinggan/camera/common/CameraFilter;-><init>(Ljava/lang/String;)V")) {
                    need(
                            selector < 0
                                    && op
                                            instanceof
                                            org.jf.dexlib2.iface.instruction.formats.Instruction35c
                                                    instruction35c
                                    && instruction35c.getRegisterD() == 2);
                    selector = i;
                }

                if (ref.equals(
                        "Landroidx/camera/video/Quality;->FHD:Landroidx/camera/video/Quality;")) {
                    need(
                            quality < 0
                                    && op instanceof OneRegisterInstruction oneRegisterInstruction
                                    && oneRegisterInstruction.getRegisterA() == 6);
                    quality = i;
                }
            }
        }

        need(selector >= 0 && quality > selector);
        b.addInstruction(
                quality + 1,
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        1,
                        6,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                HELPER,
                                "preferredQuality",
                                List.of("Ljava/lang/Object;"),
                                "Ljava/lang/Object;")));
        b.addInstruction(quality + 2, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 6));
        b.addInstruction(
                quality + 3,
                new BuilderInstruction21c(
                        Opcode.CHECK_CAST,
                        6,
                        new ImmutableTypeReference("Landroidx/camera/video/Quality;")));
        b.addInstruction(
                selector,
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        1,
                        2,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                HELPER,
                                "selectId",
                                List.of("Ljava/lang/String;"),
                                "Ljava/lang/String;")));
        b.addInstruction(selector + 1, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 2));
    }

    static byte[] apply(String profile, int api, String abi, Path apk, byte[] raw)
            throws Exception {
        validate(profile, api, abi, apk, raw);
        var before = classes(raw);
        var output = new TreeMap<String, ClassDef>();

        for (var c : before.values()) {
            if (!Set.of(SUPPORT, ACTIVITY, FILTER).contains(c.getType())) {
                output.put(c.getType(), c);
                continue;
            }

            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();

            for (var m : c.getDirectMethods()) {
                direct.add(adapt(m));
            }

            for (var m : c.getVirtualMethods()) {
                virtual.add(adapt(m));
            }

            output.put(
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

        byte[] result =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), output.values()));
        var reread = classes(result);
        need(before.keySet().equals(reread.keySet()));

        for (var e : output.entrySet()) {
            need(
                    FrameworkDexMaterializer.semantic(e.getValue())
                            .equals(FrameworkDexMaterializer.semantic(reread.get(e.getKey()))));
        }

        return result;
    }
}
