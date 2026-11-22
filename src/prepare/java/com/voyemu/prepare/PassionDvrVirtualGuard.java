package com.voyemu.prepare;

import org.jf.dexlib2.AccessFlags;
import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction10x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.reference.MethodReference;
import org.jf.dexlib2.iface.reference.StringReference;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * The pinned Drive recorder previews the virtual BACK camera instead of the DVR unit display
 * client; the recording service of the unit stays absent.
 */
final class PassionDvrVirtualGuard {
    static final String NAME = "passion-api30-dvr-virtual",
            APK = "09f3c93858cd49ab65884d115787322149e4d2a5861cb7cf314f1f6726f05294",
            HELPER_ROOT = "passion-api30-dvr-virtual/src",
            HELPER_SOURCE = "com/voyemu/passion/dvr/PassionDvrCamera.java",
            HELPER = "Lcom/voyemu/passion/dvr/PassionDvrCamera;";
    // The client and its manager live in classes27.dex, the preview manager of the app in
    // classes9.dex.
    static final String CLIENT_DEX = "classes27.dex", PREVIEW_DEX = "classes9.dex";
    static final String CLIENT = "Lcom/mega/camcorder/CamcorderNative;",
            MANAGER = "Lcom/mega/camcorder/CamcorderManager;",
            PREVIEW = "Lcom/mega/camera/camera/CameraCamcorderManager;";
    static final String LIBRARY = "dvr_display_client",
            LOAD = "Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V";
    // Native client method -> the return type of its helper.
    static final Map<String, String> NATIVES =
            Map.of(
                    "startPreview(Ljava/lang/Object;)Z", "Z",
                    "stopPreview()Z", "Z",
                    "takePicture()[B", "[B");
    // Owner of a display client loader -> the instruction count of its loadPreviewLib.
    static final Map<String, Integer> LOADERS = Map.of(MANAGER, 3, PREVIEW, 6);

    private static void require(boolean ok) {
        Failure.require(
                ok,
                "PASSION_DVR_CONTRACT",
                "Exact pinned Passion Drive recorder preview client required",
                3);
    }

    static String key(Method m) {
        return m.getName() + "(" + String.join("", m.getParameterTypes()) + ")" + m.getReturnType();
    }

    static String text(MethodReference r) {
        return r.getDefiningClass()
                + "->"
                + r.getName()
                + "("
                + String.join("", r.getParameterTypes())
                + ")"
                + r.getReturnType();
    }

    static boolean loader(Method m) {
        return LOADERS.containsKey(m.getDefiningClass())
                && m.getName().equals("loadPreviewLib")
                && m.getParameterTypes().isEmpty()
                && m.getReturnType().equals("V");
    }

    /** Index of the const-string of the display client library before its loadLibrary call. */
    static int loadIndex(List<? extends Instruction> code) {
        int found = -1;

        for (int i = 0; i + 1 < code.size(); i++) {
            if (code.get(i).getOpcode() == Opcode.CONST_STRING
                    && ((ReferenceInstruction) code.get(i)).getReference()
                            instanceof StringReference s
                    && s.getString().equals(LIBRARY)) {
                var call = code.get(i + 1);
                require(
                        found < 0
                                && call.getOpcode() == Opcode.INVOKE_STATIC
                                && text((MethodReference)
                                                ((ReferenceInstruction) call).getReference())
                                        .equals(LOAD)
                                && ((OneRegisterInstruction) code.get(i)).getRegisterA()
                                        == ((FiveRegisterInstruction) call).getRegisterC());
                found = i;
            }
        }

        return found;
    }

    static boolean loads(Method m) {
        for (var i : m.getImplementation().getInstructions()) {
            if (i.getOpcode() == Opcode.INVOKE_STATIC
                    && text((MethodReference) ((ReferenceInstruction) i).getReference())
                            .equals(LOAD)) {
                return true;
            }
        }

        return false;
    }

    static void validateLoader(Method m) {
        var impl = m.getImplementation();

        require(
                m.getAccessFlags() == AccessFlags.PUBLIC.getValue()
                        && impl != null
                        && impl.getRegisterCount() == 2
                        && impl.getTryBlocks().isEmpty());
        List<Instruction> code = new ArrayList<>();
        impl.getInstructions().forEach(code::add);
        require(code.size() == LOADERS.get(m.getDefiningClass()) && loadIndex(code) >= 0);
    }

    static void validateNative(Method m) {
        require(
                m.getAccessFlags()
                                == (AccessFlags.PUBLIC.getValue() | AccessFlags.NATIVE.getValue())
                        && m.getImplementation() == null);
    }

    static void validate(int api, Path apk, byte[] client, byte[] preview) throws Exception {
        require(
                api == 30
                        && FilesSupport.hash(apk).equals(APK)
                        && client != null
                        && preview != null);
        int natives = 0, loaders = 0;

        for (boolean isClient : new boolean[] {true, false}) {
            var dex = new DexBackedDexFile(Opcodes.forApi(30), isClient ? client : preview);

            for (var c : dex.getClasses()) {
                for (var m : c.getMethods()) {
                    if (c.getType().equals(CLIENT) && NATIVES.containsKey(key(m))) {
                        require(isClient);
                        validateNative(m);
                        natives++;
                    }

                    if (loader(m)) {
                        require(isClient != m.getDefiningClass().equals(PREVIEW));
                        validateLoader(m);
                        loaders++;
                    }
                }
            }
        }

        require(natives == NATIVES.size() && loaders == LOADERS.size());
    }

    static Method helperCall(Method m) {
        String result = NATIVES.get(key(m));
        boolean preview = m.getName().equals("startPreview");
        // Parameters: the client itself, then the preview surface of startPreview.
        var b = new MutableMethodImplementation(preview ? 3 : 2);

        b.addInstruction(
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        preview ? 2 : 1,
                        1,
                        2,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                HELPER,
                                m.getName(),
                                preview
                                        ? List.of("Ljava/lang/Object;", "Ljava/lang/Object;")
                                        : List.of("Ljava/lang/Object;"),
                                result)));
        b.addInstruction(
                new BuilderInstruction11x(
                        result.equals("Z") ? Opcode.MOVE_RESULT : Opcode.MOVE_RESULT_OBJECT, 0));
        b.addInstruction(
                new BuilderInstruction11x(
                        result.equals("Z") ? Opcode.RETURN : Opcode.RETURN_OBJECT, 0));

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                AccessFlags.PUBLIC.getValue(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(
                        b.getRegisterCount(), b.getInstructions(), List.of(), List.of()));
    }

    // NOPs of the same code units keep every address, label, position and local range.
    static Method withoutLoad(Method m) {
        var b = new MutableMethodImplementation(m.getImplementation());
        int at = loadIndex(b.getInstructions());
        int units =
                b.getInstructions().get(at).getCodeUnits()
                        + b.getInstructions().get(at + 1).getCodeUnits();

        b.replaceInstruction(at, new BuilderInstruction10x(Opcode.NOP));
        b.replaceInstruction(at + 1, new BuilderInstruction10x(Opcode.NOP));

        for (int i = 2; i < units; i++) {
            b.addInstruction(at + 2, new BuilderInstruction10x(Opcode.NOP));
        }

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(
                        b.getRegisterCount(),
                        b.getInstructions(),
                        m.getImplementation().getTryBlocks(),
                        m.getImplementation().getDebugItems()));
    }

    static ImmutableDexFile rewrite(DexFile dex) {
        var classes = new ArrayList<ClassDef>();

        for (var c : dex.getClasses()) {
            if (!c.getType().equals(CLIENT) && !LOADERS.containsKey(c.getType())) {
                classes.add(c);
                continue;
            }

            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();

            for (var m : c.getDirectMethods()) {
                direct.add(loader(m) ? withoutLoad(m) : m);
            }

            for (var m : c.getVirtualMethods()) {
                virtual.add(
                        c.getType().equals(CLIENT) && NATIVES.containsKey(key(m))
                                ? helperCall(m)
                                : loader(m) ? withoutLoad(m) : m);
            }

            classes.add(
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

        return new ImmutableDexFile(Opcodes.forApi(30), classes);
    }

    // Serialization reorders classes, so the untouched ones compare by type.
    static Map<String, String> others(DexFile dex) throws Exception {
        var kept = new TreeMap<String, String>();

        for (var c : dex.getClasses()) {
            if (!c.getType().equals(CLIENT) && !LOADERS.containsKey(c.getType())) {
                kept.put(c.getType(), FrameworkDexMaterializer.semantic(c));
            }
        }

        return kept;
    }

    static byte[] transform(byte[] bytes) throws Exception {
        var before = new DexBackedDexFile(Opcodes.forApi(30), bytes);
        byte[] out = FrameworkDexMaterializer.serialize(rewrite(before));
        var after = new DexBackedDexFile(Opcodes.forApi(30), out);

        require(others(before).equals(others(after)));

        for (var c : after.getClasses()) {
            for (var m : c.getMethods()) {
                if (c.getType().equals(CLIENT)) {
                    require(
                            (m.getAccessFlags() & AccessFlags.NATIVE.getValue()) == 0
                                    || !NATIVES.containsKey(key(m)));
                }

                if (loader(m)) {
                    require(!loads(m));
                }
            }
        }

        return out;
    }

    /** Returns the rewritten client and preview DEX entries, in this order. */
    static List<byte[]> apply(int api, Path apk, byte[] client, byte[] preview) throws Exception {
        validate(api, apk, client, preview);

        return List.of(transform(client), transform(preview));
    }
}
