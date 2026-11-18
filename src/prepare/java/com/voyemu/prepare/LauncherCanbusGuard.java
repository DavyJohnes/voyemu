package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.MethodImplementation;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction22c;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction35c;
import org.jf.dexlib2.immutable.reference.ImmutableFieldReference;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/**
 * Preserve the genuine CAN initialization callback; read its first snapshot only once connected.
 */
final class LauncherCanbusGuard {
    static final String NAME = "api30-launcher-canbus";

    static final Set<String> PROFILES =
            Set.of("free-11-chinese-20250710-6.11.1", "free-11-chinese-20260731-7.3.1");

    static final String OWNER = "Lcom/qinggan/launcher/base/manager/CompassDataManager;",
            REGISTER = "registerCanBusListener",
            CALLBACK = "lambda$registerCanBusListener$0$CompassDataManager",
            CAN = "Lcom/qinggan/canbus/CanBusManager;",
            GET = CAN + "->getInstance()" + CAN;

    static List<String> pins(String profile) {
        return PROFILES.contains(profile) ? LauncherPowerGuard.pins(profile) : List.of();
    }

    static void need(boolean v) {
        Failure.require(
                v, "LAUNCHER_CANBUS", "Exact asynchronous Compass CAN boundary required", 3);
    }

    static Map<String, ClassDef> classes(byte[] b) {
        var out = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), b).getClasses()) {
            need(out.put(c.getType(), c) == null);
        }

        return out;
    }

    static Method method(Map<String, ClassDef> cs, String name) {
        Method found = null;
        var c = cs.get(OWNER);

        need(c != null);

        for (var m : c.getMethods()) {
            if (m.getName().equals(name)) {
                need(found == null);
                found = m;
            }
        }

        need(found != null);

        return found;
    }

    static byte[] original(String profile, Path apk) throws Exception {
        try (var zip = new java.util.zip.ZipFile(apk.toFile())) {
            var entry = zip.getEntry("classes2.dex");
            need(entry != null);

            try (var in = zip.getInputStream(entry)) {
                return in.readAllBytes();
            }
        }
    }

    static void validate(String profile, int api, String abi, Path apk, byte[] source)
            throws Exception {
        need(
                PROFILES.contains(profile)
                        && api == 30
                        && abi.equals("arm64-v8a")
                        && LauncherPowerGuard.pins(profile).contains(FilesSupport.hash(apk))
                        && source != null);
        var cs = classes(source);
        method(cs, REGISTER);
        method(cs, CALLBACK);
    }

    static ImmutableMethod replace(Method m, MethodImplementation code) {
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

    static byte[] apply(String profile, int api, String abi, Path apk, byte[] source)
            throws Exception {
        validate(profile, api, abi, apk, source);
        var before = classes(source);
        var register = method(before, REGISTER);
        var callback = method(before, CALLBACK);
        var original = new ArrayList<Instruction>();
        register.getImplementation().getInstructions().forEach(original::add);
        int start = -1;

        for (int i = 0; i < original.size(); i++) {
            if (original.get(i) instanceof ReferenceInstruction r
                    && r.getReference().toString().equals(GET)) {
                need(start < 0);
                start = i;
            }
        }

        need(start >= 0 && original.get(original.size() - 1).getOpcode() == Opcode.RETURN_VOID);
        var registration = new MutableMethodImplementation(register.getImplementation());

        while (registration.getInstructions().size() > start + 1) {
            registration.removeInstruction(start);
        }

        var delayed = new ArrayList<Instruction>();
        delayed.add(
                new ImmutableInstruction22c(
                        Opcode.IGET_OBJECT,
                        0,
                        8,
                        new ImmutableFieldReference(OWNER, "mCanBusManager", CAN)));
        delayed.add(new ImmutableInstruction10x(Opcode.NOP));
        delayed.add(new ImmutableInstruction10x(Opcode.NOP));
        delayed.add(
                new ImmutableInstruction22c(
                        Opcode.IGET_OBJECT,
                        1,
                        8,
                        new ImmutableFieldReference(
                                OWNER, "mCanBusListener", "Lcom/qinggan/canbus/CanBusListener;")));
        delayed.add(
                new ImmutableInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        2,
                        0,
                        1,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                CAN,
                                "registerCanBusListener",
                                List.of("Lcom/qinggan/canbus/CanBusListener;"),
                                "V")));

        for (int i = start; i < original.size(); i++) {
            delayed.add(ImmutableInstruction.of(original.get(i)));
        }

        var connected =
                new MutableMethodImplementation(
                        new ImmutableMethodImplementation(10, delayed, List.of(), List.of()));
        var end = connected.newLabelForIndex(connected.getInstructions().size() - 1);
        connected.replaceInstruction(1, new BuilderInstruction21t(Opcode.IF_EQZ, 0, end));
        connected.replaceInstruction(2, new BuilderInstruction21t(Opcode.IF_EQZ, 9, end));
        var replacements =
                Map.of(
                        REGISTER,
                        replace(register, registration),
                        CALLBACK,
                        replace(callback, connected));
        var out = new TreeMap<String, ClassDef>();

        for (var c : before.values()) {
            if (!c.getType().equals(OWNER)) {
                out.put(c.getType(), c);
                continue;
            }

            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();

            for (var m : c.getDirectMethods()) {
                direct.add(
                        replacements.containsKey(m.getName()) ? replacements.get(m.getName()) : m);
            }

            for (var m : c.getVirtualMethods()) {
                virtual.add(
                        replacements.containsKey(m.getName()) ? replacements.get(m.getName()) : m);
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

        byte[] result =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), out.values()));
        var reread = classes(result);
        need(reread.keySet().equals(before.keySet()));

        for (var e : out.entrySet()) {
            need(
                    FrameworkDexMaterializer.semantic(e.getValue())
                            .equals(FrameworkDexMaterializer.semantic(reread.get(e.getKey()))));
        }

        return result;
    }
}
