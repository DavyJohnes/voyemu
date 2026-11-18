package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/**
 * Inactive Free A11 7.3.1 adapter, applied only after the existing exact clean navigation adapter.
 */
final class HiboardTopGestureGuard {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1",
            PIN = "c6992cdc2a3e23d5aeb6ab77e7a7f6889cfcf5c262c6ed86825e6fcbf601681d";
    static final String POLICY = "Lcom/android/server/wm/DisplayPolicy;",
            OWNER = "Lcom/android/server/wm/DisplayPolicy$2;",
            HELPER = "Lcom/android/server/wm/HiboardTopGesture;";
    static final String TARGET = OWNER + "->onSwipeFromTop()V";
    static final List<String> PARAMETERS = List.of(POLICY, "Lcom/android/server/wm/WindowState;");

    static void require(boolean ok) {
        Failure.require(
                ok,
                "HIBOARD_TOP_GESTURE",
                "Exact Free A11 7.3.1 clean API30 top-gesture adapter required",
                3);
    }

    static String ref(boolean routed) {
        return (routed ? HELPER : POLICY)
                + "->"
                + (routed ? "request" : "access$400")
                + "("
                + String.join("", PARAMETERS)
                + ")V";
    }

    static Map<String, ClassDef> classes(byte[] dex) {
        var out = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            require(out.put(c.getType(), c) == null);
        }

        return out;
    }

    static void validateMethod(Method m) throws Exception {
        require(
                m.toString().equals(TARGET)
                        && m.getAccessFlags() == 1
                        && m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == 4
                        && m.getImplementation().getTryBlocks().size() == 1);
        rewrite(m, true);
    }

    static Method rewrite(Method m, boolean forward) {
        require(
                m.toString().equals(TARGET)
                        && m.getImplementation() != null
                        && m.getImplementation().getRegisterCount() == 4);
        var code = new MutableMethodImplementation(m.getImplementation());
        int at = 0, changed = 0;

        for (int i = 0; i < code.getInstructions().size(); i++) {
            var x = code.getInstructions().get(i);

            if (at == 0x17) {
                require(
                        x.getOpcode() == Opcode.INVOKE_STATIC
                                && x instanceof FiveRegisterInstruction
                                && x instanceof ReferenceInstruction);
                var v = (FiveRegisterInstruction) x;
                require(
                        v.getRegisterCount() == 2
                                && v.getRegisterC() == 1
                                && v.getRegisterD() == 2
                                && v.getRegisterE() == 0
                                && v.getRegisterF() == 0
                                && v.getRegisterG() == 0
                                && ((ReferenceInstruction) x)
                                        .getReference()
                                        .toString()
                                        .equals(ref(!forward)));
                code.replaceInstruction(
                        i,
                        new BuilderInstruction35c(
                                Opcode.INVOKE_STATIC,
                                2,
                                1,
                                2,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(
                                        forward ? HELPER : POLICY,
                                        forward ? "request" : "access$400",
                                        PARAMETERS,
                                        "V")));
                changed++;
            }

            at += x.getCodeUnits();
        }

        require(changed == 1 && at == 31);

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

    static ClassDef rewriteClass(ClassDef c, boolean forward) {
        var direct = new ArrayList<Method>();
        var virtual = new ArrayList<Method>();

        for (var m : c.getDirectMethods()) {
            direct.add(m.toString().equals(TARGET) ? rewrite(m, forward) : m);
        }

        for (var m : c.getVirtualMethods()) {
            virtual.add(m.toString().equals(TARGET) ? rewrite(m, forward) : m);
        }

        return new ImmutableClassDef(
                c.getType(),
                c.getAccessFlags(),
                c.getSuperclass(),
                c.getInterfaces(),
                c.getSourceFile(),
                c.getAnnotations(),
                c.getStaticFields(),
                c.getInstanceFields(),
                direct,
                virtual);
    }

    static final Set<String> SOURCES =
            Set.of(
                    "src/com/android/server/wm/HiboardTopGesture.java",
                    "src/compile-only/com/android/server/wm/Api30FrameworkSignatures.java",
                    "src/compile-only/android/os/SystemProperties.java",
                    "src/compile-only/android/view/DisplayInfo.java");

    static void validateOriginal(String profile, int api, Path original, Path source)
            throws Exception {
        require(PROFILE.equals(profile) && api == 30 && FilesSupport.hash(original).equals(PIN));

        try (var jar = new java.util.zip.ZipFile(original.toFile())) {
            require(jar.getEntry("classes2.dex") != null);
        }

        var files = FilesSupport.tree(source);
        require(files.keySet().equals(SOURCES));
    }

    static byte[] buildHelper(
            String profile, int api, Path original, Path source, Path platform, Path work)
            throws Exception {
        validateOriginal(profile, api, original, source);
        Path output =
                GuestJava.dex(
                        new GuestJava.Unit(source.resolve("src"), api),
                        platform,
                        work.resolve("helper"));
        byte[] bytes = java.nio.file.Files.readAllBytes(output.resolve("classes.dex"));
        validateOriginal(profile, api, original, source);

        return bytes;
    }

    static byte[] apply(String profile, int api, Path original, byte[] predecessor, byte[] helper)
            throws Exception {
        require(
                PROFILE.equals(profile)
                        && api == 30
                        && FilesSupport.hash(original).equals(PIN)
                        && predecessor != null
                        && helper != null);
        var before = classes(predecessor);
        var additions = classes(helper);
        require(
                additions
                        .keySet()
                        .equals(
                                Set.of(
                                        HELPER,
                                        HELPER.substring(0, HELPER.length() - 1) + "$Candidate;")));
        var changed = new TreeMap<String, ClassDef>();
        int found = 0;

        for (var c : before.values()) {
            for (var m : c.getMethods()) {
                if (m.toString().equals(TARGET)) {
                    validateMethod(m);
                    found++;
                }
            }

            changed.put(c.getType(), rewriteClass(c, true));
        }

        require(found == 1);

        for (var e : additions.entrySet()) {
            require(changed.put(e.getKey(), e.getValue()) == null);
        }

        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), changed.values()));
        var reread = classes(output);
        require(reread.keySet().equals(changed.keySet()));

        for (var e : reread.entrySet()) {
            require(
                    FrameworkDexMaterializer.semantic(e.getValue())
                            .equals(FrameworkDexMaterializer.semantic(changed.get(e.getKey()))));

            if (before.containsKey(e.getKey())) {
                require(
                        FrameworkDexMaterializer.semantic(before.get(e.getKey()))
                                .equals(
                                        FrameworkDexMaterializer.semantic(
                                                rewriteClass(e.getValue(), false))));
            }
        }

        require(FilesSupport.hash(original).equals(PIN));

        return output;
    }
}
