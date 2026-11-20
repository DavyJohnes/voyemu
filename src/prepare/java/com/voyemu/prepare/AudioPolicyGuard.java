package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction11x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21c;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21s;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction22c;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction22x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction23x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction35c;
import org.jf.dexlib2.immutable.reference.ImmutableFieldReference;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableStringReference;
import org.jf.dexlib2.immutable.reference.ImmutableTypeReference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

/** Private exact-input prototype; the declared step admits the original by its pin. */
final class AudioPolicyGuard {
    static final String SERVICE = "Lcom/qinggan/audiopolicy/service/OSAudioPolicyService;",
            STUB = "Lcom/qinggan/audiopolicy/service/OSAudioPolicyService$1;",
            HELPER = "Lcom/voyemu/oem/audio/OemAudioPolicy;",
            BINDER = "Landroid/os/IBinder;",
            FIELD = "Lcom/qinggan/audiopolicy/IAudioPolicyService$Stub;";
    static final String HELPER_DIRECTORY = "src/guest/android/oem/audio";
    static final List<String> HELPER_SOURCES =
            List.of(
                    HELPER_DIRECTORY + "/OemAudioPolicy.java",
                    HELPER_DIRECTORY + "/OemAudioPolicyUnavailable.java");

    private static Path helperSource;

    // The recipes, which run on the source tree instead of the helper JAR, take them from it.
    static synchronized void useHelperSource(Path directory) {
        helperSource = directory;
    }

    /**
     * The AudioPolicy helper sources that the helper JAR embeds, written once to a temporary
     * directory; preparation never reads src/.
     */
    static synchronized Path helperSource() throws Exception {
        if (helperSource == null) {
            Path directory = Files.createTempDirectory("voyemu-audio-policy");

            for (String source : HELPER_SOURCES) {
                Files.write(
                        directory.resolve(Path.of(source).getFileName()),
                        ProfileRegistry.bytes("voyemu/" + source));
            }

            helperSource = directory;
        }

        return helperSource;
    }

    static void validateHelperSources(Path source) throws Exception {
        Set<String> sources = new TreeSet<>();

        for (String name : GuestJava.sources(source)) {
            sources.add(HELPER_DIRECTORY + "/" + name);
        }

        require(sources.equals(Set.copyOf(HELPER_SOURCES)));
    }

    static void require(boolean b) {
        Failure.require(
                b,
                "AUDIO_POLICY_CONTRACT",
                "Exact AudioPolicy input and semantic contract required",
                3);
    }

    static String signature(Method m) {
        StringBuilder s = new StringBuilder(m.getName()).append('(');

        for (var p : m.getParameterTypes()) {
            s.append(p);
        }

        return s.append(')').append(m.getReturnType()).toString();
    }

    static String key(Method m) {
        return m.getDefiningClass() + "->" + signature(m);
    }

    static Map<String, ClassDef> classes(byte[] d) {
        var out = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), d).getClasses()) {
            require(out.put(c.getType(), c) == null);
        }

        return out;
    }

    static boolean target(Method m) {
        return (m.getDefiningClass().equals(STUB) && (m.getAccessFlags() & 1) != 0)
                || (m.getDefiningClass().equals(SERVICE)
                        && (m.getName().equals("onCreate") || m.getName().equals("onDestroy")));
    }

    static ImmutableMethodReference ref(
            String owner, String name, List<String> params, String ret) {
        return new ImmutableMethodReference(owner, name, params, ret);
    }

    static Instruction call(Opcode op, int[] regs, ImmutableMethodReference r) {
        require(regs.length <= 5);

        for (int n : regs) {
            require(n < 16);
        }

        int[] a = new int[5];
        System.arraycopy(regs, 0, a, 0, regs.length);

        return new ImmutableInstruction35c(op, regs.length, a[0], a[1], a[2], a[3], a[4], r);
    }

    static Method with(Method m, int registers, List<Instruction> code) {
        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(registers, code, List.of(), List.of()));
    }

    static Instruction field(int out, int self) {
        return new ImmutableInstruction22c(
                Opcode.IGET_OBJECT, out, self, new ImmutableFieldReference(SERVICE, "c", FIELD));
    }

    static Method rewrite(Method m) {
        if (m.getDefiningClass().equals(SERVICE)) {
            if (m.getName().equals("onDestroy")) {
                var b = new MutableMethodImplementation(m.getImplementation());
                int self = b.getRegisterCount() - 1;
                require(self < 16);
                b.addInstruction(
                        0,
                        new BuilderInstruction22c(
                                Opcode.IGET_OBJECT,
                                0,
                                self,
                                new ImmutableFieldReference(SERVICE, "c", FIELD)));
                b.addInstruction(
                        1,
                        new BuilderInstruction35c(
                                Opcode.INVOKE_STATIC,
                                1,
                                0,
                                0,
                                0,
                                0,
                                0,
                                ref(HELPER, "stop", List.of(BINDER), "V")));

                return PassionServiceForegroundGuard.copy(m, b);
            }

            return with(
                    m,
                    4,
                    List.of(
                            call(
                                    Opcode.INVOKE_SUPER,
                                    new int[] {3},
                                    ref("Landroid/app/Service;", "onCreate", List.of(), "V")),
                            field(0, 3),
                            call(
                                    Opcode.INVOKE_STATIC,
                                    new int[] {3, 0},
                                    ref(
                                            HELPER,
                                            "start",
                                            List.of("Landroid/content/Context;", BINDER),
                                            "V")),
                            new ImmutableInstruction21c(
                                    Opcode.CONST_STRING,
                                    1,
                                    new ImmutableStringReference("qg.audio_policy")),
                            call(
                                    Opcode.INVOKE_STATIC,
                                    new int[] {1, 0},
                                    ref(
                                            "Lcom/qinggan/os/ServiceManager;",
                                            "addService",
                                            List.of("Ljava/lang/String;", BINDER),
                                            "V")),
                            call(
                                    Opcode.INVOKE_DIRECT,
                                    new int[] {3},
                                    ref(SERVICE, "a", List.of(), "V")),
                            new ImmutableInstruction10x(Opcode.RETURN_VOID)));
        }

        int n = m.getParameterTypes().size(), self = 6;
        var code = new ArrayList<Instruction>();
        // Scratch v0 original Binder, v1 signature, v2 array, v3 value, v4 index, v5 size.
        code.add(new ImmutableInstruction22x(Opcode.MOVE_OBJECT_FROM16, 0, self));
        code.add(
                new ImmutableInstruction21c(
                        Opcode.CONST_STRING, 1, new ImmutableStringReference(signature(m))));
        code.add(new ImmutableInstruction21s(Opcode.CONST_16, 5, n));
        code.add(
                new ImmutableInstruction22c(
                        Opcode.NEW_ARRAY, 2, 5, new ImmutableTypeReference("[Ljava/lang/Object;")));

        for (int i = 0; i < n; i++) {
            String t = m.getParameterTypes().get(i).toString();
            require(!t.equals("J") && !t.equals("D"));
            int p = self + i + 1;

            if (t.equals("I") || t.equals("Z")) {
                code.add(new ImmutableInstruction22x(Opcode.MOVE_FROM16, 3, p));
                String box = t.equals("I") ? "Ljava/lang/Integer;" : "Ljava/lang/Boolean;";
                code.add(
                        call(
                                Opcode.INVOKE_STATIC,
                                new int[] {3},
                                ref(box, "valueOf", List.of(t), box)));
                code.add(new ImmutableInstruction11x(Opcode.MOVE_RESULT_OBJECT, 3));
            } else {
                require(t.startsWith("L") || t.startsWith("["));
                code.add(new ImmutableInstruction22x(Opcode.MOVE_OBJECT_FROM16, 3, p));
            }

            code.add(new ImmutableInstruction21s(Opcode.CONST_16, 4, i));
            code.add(new ImmutableInstruction23x(Opcode.APUT_OBJECT, 3, 2, 4));
        }

        code.add(
                call(
                        Opcode.INVOKE_STATIC,
                        new int[] {0, 1, 2},
                        ref(
                                HELPER,
                                "dispatch",
                                List.of(BINDER, "Ljava/lang/String;", "[Ljava/lang/Object;"),
                                "Ljava/lang/Object;")));
        String ret = m.getReturnType();

        if (ret.equals("V")) {
            code.add(new ImmutableInstruction10x(Opcode.RETURN_VOID));
        } else {
            code.add(new ImmutableInstruction11x(Opcode.MOVE_RESULT_OBJECT, 3));

            if (ret.equals("I") || ret.equals("Z")) {
                String box = ret.equals("I") ? "Ljava/lang/Integer;" : "Ljava/lang/Boolean;";
                code.add(
                        new ImmutableInstruction21c(
                                Opcode.CHECK_CAST, 3, new ImmutableTypeReference(box)));
                code.add(
                        call(
                                Opcode.INVOKE_VIRTUAL,
                                new int[] {3},
                                ref(
                                        box,
                                        ret.equals("I") ? "intValue" : "booleanValue",
                                        List.of(),
                                        ret)));
                code.add(new ImmutableInstruction11x(Opcode.MOVE_RESULT, 3));
                code.add(new ImmutableInstruction11x(Opcode.RETURN, 3));
            } else {
                require(ret.startsWith("L") || ret.startsWith("["));
                code.add(
                        new ImmutableInstruction21c(
                                Opcode.CHECK_CAST, 3, new ImmutableTypeReference(ret)));
                code.add(new ImmutableInstruction11x(Opcode.RETURN_OBJECT, 3));
            }
        }

        return with(m, 7 + n, code);
    }

    static void validate(Map<String, Object> constants, int api, String abi, Path apk, byte[] dex)
            throws Exception {
        require(
                api == 30
                        && abi.equals("arm64-v8a")
                        && dex != null
                        && StepConstants.admits(constants, FilesSupport.hash(apk)));
        transform(classes(dex), null);
    }

    static Map<String, ClassDef> transform(
            Map<String, ClassDef> source, Map<String, Method> restore) throws Exception {
        var out = new TreeMap<String, ClassDef>();
        int count = 0;

        for (var c : source.values()) {
            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();
            int group = 0;

            for (var methods : List.of(c.getDirectMethods(), c.getVirtualMethods())) {
                var dest = group++ == 0 ? direct : virtual;

                for (var m : methods) {
                    if (target(m)) {
                        m = restore == null ? rewrite(m) : restore.get(key(m));
                        require(m != null);
                        count++;
                    }

                    dest.add(m);
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

        require(count == 118);

        return out;
    }

    static void equal(Map<String, ClassDef> a, Map<String, ClassDef> b) throws Exception {
        require(a.keySet().equals(b.keySet()));

        for (String k : a.keySet()) {
            require(
                    FrameworkDexMaterializer.semantic(a.get(k))
                            .equals(FrameworkDexMaterializer.semantic(b.get(k))));
        }
    }

    static byte[] apply(Map<String, Object> constants, int api, String abi, Path apk, byte[] dex)
            throws Exception {
        validate(constants, api, abi, apk, dex);
        var before = classes(dex);
        var expected = transform(before, null);
        byte[] result =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), expected.values()));
        var actual = classes(result);
        equal(expected, actual);
        // Restore only after every transformed class matches the deterministic expected semantics.
        var originals = new TreeMap<String, Method>();

        for (var c : before.values()) {
            for (var m : c.getMethods()) {
                if (target(m)) {
                    originals.put(key(m), m);
                }
            }
        }

        equal(before, transform(actual, originals));
        require(StepConstants.admits(constants, FilesSupport.hash(apk)));

        return result;
    }
}
