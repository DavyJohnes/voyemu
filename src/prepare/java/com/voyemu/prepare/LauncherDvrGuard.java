package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/** Only the Launcher DVR entries may use an actually installed virtual BACK camera. */
final class LauncherDvrGuard {
    static final String NAME = "api30-launcher-dvr",
            OWNER = "Lcom/qinggan/launcher/base/utils/AppUtils;",
            ROUTE = "Lcom/qinggan/launcher/base/utils/AppLauncher;",
            HELPER = "Lcom/voyemu/launcher/DvrAvailability;";
    static final String HELPER_ROOT = "api30-launcher-dvr/src",
            HELPER_PATH = HELPER_ROOT + "/com/voyemu/launcher/DvrAvailability.java";

    static void need(boolean v) {
        Failure.require(v, "LAUNCHER_DVR", "Exact Launcher DVR boundary required", 3);
    }

    static Map<String, ClassDef> classes(byte[] b) {
        var out = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), b).getClasses()) {
            need(out.put(c.getType(), c) == null);
        }

        return out;
    }

    /**
     * True selects the fixed instruction indices of a Launcher classes2.dex whose DVR reads are
     * pinned, false locates the DVR reads by scan.
     */
    static boolean pinned(Map<String, Object> constants) {
        return StepConstants.flag(constants, "launcher_dvr_pinned");
    }

    static void validate(Map<String, Object> constants, int api, String abi, Path apk, byte[] raw)
            throws Exception {
        if (pinned(constants)) {
            Pinned.validate(constants, api, abi, apk, raw);
        } else {
            Recomputed.validate(constants, api, abi, apk, raw);
        }
    }

    static void validateOriginalChain(
            Map<String, Object> constants, int api, String abi, Path apk, byte[] raw)
            throws Exception {
        if (pinned(constants)) {
            Pinned.validate(constants, api, abi, apk, raw);
        } else {
            Recomputed.validateOriginalChain(constants, api, abi, apk, raw);
        }
    }

    static byte[] apply(Map<String, Object> constants, int api, String abi, Path apk, byte[] raw)
            throws Exception {
        return pinned(constants)
                ? Pinned.apply(constants, api, abi, apk, raw)
                : Recomputed.apply(constants, api, abi, apk, raw);
    }

    /** All three config reads are located by scan; the input must equal the recomputed chain. */
    static final class Recomputed {
        static final String
                DVR =
                        "Lcom/qinggan/vehicle/VehicleOnlineState;->DVR:Lcom/qinggan/vehicle/VehicleOnlineState;",
                READ =
                        "Lcom/qinggan/vehicle/VehicleConfigHelper;->readOnlineConfigItem(Lcom/qinggan/vehicle/VehicleOnlineState;)I";

        private Recomputed() {}

        static Method method(Map<String, ClassDef> cs, String owner, String name) {
            var c = cs.get(owner);

            need(c != null);
            Method found = null;

            for (var m : c.getMethods()) {
                if (m.getName().equals(name)) {
                    need(found == null);
                    found = m;
                }
            }

            need(found != null && found.getImplementation() != null);

            return found;
        }

        static byte[] original(Path apk) throws Exception {
            try (var zip = new java.util.zip.ZipFile(apk.toFile())) {
                var entry = zip.getEntry("classes2.dex");
                need(entry != null);

                return zip.getInputStream(entry).readAllBytes();
            }
        }

        static void validate(
                Map<String, Object> constants, int api, String abi, Path apk, byte[] raw)
                throws Exception {
            need(
                    api == 30
                            && "arm64-v8a".equals(abi)
                            && StepConstants.admits(constants, FilesSupport.hash(apk))
                            && raw != null);
            byte[] source = original(apk);
            ViewDumpPatch.checkedDex(raw, NativeCore.hash(raw));
            var current = classes(raw);
            var before = classes(source);

            for (String name : List.of("generateApp", "generateHomeCardApp")) {
                need(
                        FrameworkDexMaterializer.semantic(method(current, OWNER, name))
                                .equals(
                                        FrameworkDexMaterializer.semantic(
                                                method(before, OWNER, name))));
            }

            need(
                    FrameworkDexMaterializer.semantic(method(current, ROUTE, "startAppBySubType"))
                            .equals(
                                    FrameworkDexMaterializer.semantic(
                                            method(before, ROUTE, "startAppBySubType"))));
            adapt(method(current, OWNER, "generateApp"));
            adapt(method(current, OWNER, "generateHomeCardApp"));
            adaptRoute(method(current, ROUTE, "startAppBySubType"));
        }

        static void validateOriginalChain(
                Map<String, Object> constants, int api, String abi, Path apk, byte[] raw)
                throws Exception {
            need(
                    api == 30
                            && "arm64-v8a".equals(abi)
                            && StepConstants.admits(constants, FilesSupport.hash(apk))
                            && raw != null
                            && Arrays.equals(raw, original(apk)));
            byte[] power = LauncherPowerGuard.apply(constants, api, apk, raw, "classes2.dex");
            if (AqyProviderGuard.present(power)) {
                power = AqyProviderGuard.apply(constants, api, apk, power);
            }

            validate(constants, api, abi, apk, power);
        }

        static Method adapt(Method m) {
            var b = new MutableMethodImplementation(m.getImplementation());
            var positions = new ArrayList<Integer>();
            var regs = new ArrayList<Integer>();
            var code = b.getInstructions();

            for (int i = 0; i + 2 < code.size(); i++) {
                if (code.get(i) instanceof ReferenceInstruction r
                        && r.getReference().toString().equals(DVR)) {
                    need(
                            code.get(i).getOpcode() == Opcode.SGET_OBJECT
                                    && code.get(i + 1).getOpcode() == Opcode.INVOKE_STATIC
                                    && code.get(i + 1) instanceof ReferenceInstruction call
                                    && call.getReference().toString().equals(READ)
                                    && code.get(i + 2).getOpcode() == Opcode.MOVE_RESULT
                                    && code.get(i + 2) instanceof OneRegisterInstruction);
                    positions.add(i + 2);
                    regs.add(((OneRegisterInstruction) code.get(i + 2)).getRegisterA());
                }
            }

            need(positions.size() == (m.getName().equals("generateApp") ? 2 : 1));

            for (int i = positions.size() - 1; i >= 0; i--) {
                int pos = positions.get(i), reg = regs.get(i);
                need(reg <= 15);
                b.addInstruction(
                        pos + 1,
                        new BuilderInstruction35c(
                                Opcode.INVOKE_STATIC,
                                1,
                                reg,
                                0,
                                0,
                                0,
                                0,
                                new ImmutableMethodReference(HELPER, "select", List.of("I"), "I")));
                b.addInstruction(pos + 2, new BuilderInstruction11x(Opcode.MOVE_RESULT, reg));
            }

            return PassionServiceForegroundGuard.copy(m, b);
        }

        static int indexAt(MutableMethodImplementation b, int address) {
            int at = 0;

            for (int i = 0; i < b.getInstructions().size(); i++) {
                if (at == address) {
                    return i;
                }

                at += b.getInstructions().get(i).getCodeUnits();
            }

            throw new Failure("LAUNCHER_DVR", "Missing exact DVR route address", 3);
        }

        static Method adaptRoute(Method m) {
            var b = new MutableMethodImplementation(m.getImplementation());

            need(b.getRegisterCount() == 11);
            int serviceIndex = indexAt(b, 0x1af), tagIndex = indexAt(b, 0x1cf);
            var code = b.getInstructions();
            need(
                    code.get(serviceIndex).getOpcode() == Opcode.NEW_INSTANCE
                            && code.get(tagIndex).getOpcode() == Opcode.SGET_OBJECT
                            && ((OneRegisterInstruction) code.get(tagIndex)).getRegisterA() == 9
                            && code.get(tagIndex) instanceof ReferenceInstruction);
            var field = ((ReferenceInstruction) code.get(tagIndex)).getReference();
            need(field.toString().equals(ROUTE + "->TAG:Ljava/lang/String;"));
            var service = b.newLabelForIndex(serviceIndex);
            b.replaceInstruction(
                    tagIndex,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_STATIC,
                            1,
                            2,
                            0,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    HELPER, "useService", List.of("Ljava/lang/Object;"), "Z")));
            b.addInstruction(tagIndex + 1, new BuilderInstruction11x(Opcode.MOVE_RESULT, 0));
            b.addInstruction(tagIndex + 2, new BuilderInstruction21t(Opcode.IF_NEZ, 0, service));
            b.addInstruction(tagIndex + 3, new BuilderInstruction21c(Opcode.SGET_OBJECT, 9, field));

            return PassionServiceForegroundGuard.copy(m, b);
        }

        static byte[] apply(
                Map<String, Object> constants, int api, String abi, Path apk, byte[] raw)
                throws Exception {
            validate(constants, api, abi, apk, raw);
            var before = classes(raw);
            var out = new TreeMap<String, ClassDef>();

            for (var c : before.values()) {
                if (!c.getType().equals(OWNER) && !c.getType().equals(ROUTE)) {
                    out.put(c.getType(), c);
                    continue;
                }

                var direct = new ArrayList<Method>();
                var virtual = new ArrayList<Method>();
                int gi = 0;

                for (var group : List.of(c.getDirectMethods(), c.getVirtualMethods())) {
                    var dst = gi++ == 0 ? direct : virtual;

                    for (var m : group) {
                        Method next = m;

                        if (c.getType().equals(OWNER)
                                && List.of("generateApp", "generateHomeCardApp")
                                        .contains(m.getName())) {
                            next = adapt(m);
                        }

                        if (c.getType().equals(ROUTE) && m.getName().equals("startAppBySubType")) {
                            next = adaptRoute(m);
                        }

                        dst.add(next);
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

            byte[] result =
                    FrameworkDexMaterializer.serialize(
                            new ImmutableDexFile(Opcodes.forApi(30), out.values()));
            var reread = classes(result);
            need(before.keySet().equals(reread.keySet()));

            for (var e : out.entrySet()) {
                need(
                        FrameworkDexMaterializer.semantic(e.getValue())
                                .equals(FrameworkDexMaterializer.semantic(reread.get(e.getKey()))));
            }

            return result;
        }
    }

    /** Fixed instruction indices of the pinned DEX and its predecessors. */
    static final class Pinned {
        static final Set<String> METHODS = Set.of("generateApp", "generateHomeCardApp");

        private Pinned() {}

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

        static Method route(Map<String, ClassDef> cs) {
            var c = cs.get("Lcom/qinggan/launcher/base/utils/AppLauncher;");

            need(c != null);
            Method found = null;

            for (var m : c.getMethods()) {
                if (m.getName().equals("startAppBySubType")) {
                    need(found == null);
                    found = m;
                }
            }

            need(found != null);

            return found;
        }

        static void validate(
                Map<String, Object> constants, int api, String abi, Path apk, byte[] raw)
                throws Exception {
            need(
                    api == 30
                            && "arm64-v8a".equals(abi)
                            && StepConstants.admits(constants, FilesSupport.hash(apk))
                            && raw != null);
            var cs = classes(raw);

            for (var name : METHODS) {
                method(cs, name);
            }

            route(cs);
        }

        static Method adapt(Method m) {
            int at = m.getName().equals("generateApp") ? 880 : 105,
                    reg = m.getName().equals("generateApp") ? 1 : 7;
            var b = new MutableMethodImplementation(m.getImplementation());

            need(
                    b.getInstructions().get(at).getOpcode() == Opcode.MOVE_RESULT
                            && ((OneRegisterInstruction) b.getInstructions().get(at)).getRegisterA()
                                    == reg);
            need(
                    b.getInstructions().get(at - 1) instanceof ReferenceInstruction r
                            && r.getReference()
                                    .toString()
                                    .equals(
                                            "Lcom/qinggan/vehicle/VehicleConfigHelper;->readOnlineConfigItem(Lcom/qinggan/vehicle/VehicleOnlineState;)I"));
            need(
                    b.getInstructions().get(at - 2) instanceof ReferenceInstruction r
                            && r.getReference()
                                    .toString()
                                    .equals(
                                            "Lcom/qinggan/vehicle/VehicleOnlineState;->DVR:Lcom/qinggan/vehicle/VehicleOnlineState;"));
            b.addInstruction(
                    at + 1,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_STATIC,
                            1,
                            reg,
                            0,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(HELPER, "select", List.of("I"), "I")));
            b.addInstruction(at + 2, new BuilderInstruction11x(Opcode.MOVE_RESULT, reg));

            return new ImmutableMethod(
                    m.getDefiningClass(),
                    m.getName(),
                    m.getParameters(),
                    m.getReturnType(),
                    m.getAccessFlags(),
                    m.getAnnotations(),
                    m.getHiddenApiRestrictions(),
                    b);
        }

        static Method adaptRoute(Method m) {
            var b = new MutableMethodImplementation(m.getImplementation());

            need(b.getRegisterCount() == 11);
            var code = b.getInstructions();
            need(
                    code.get(234).getOpcode() == Opcode.NEW_INSTANCE
                            && code.get(247).getOpcode() == Opcode.SGET_OBJECT
                            && ((OneRegisterInstruction) code.get(247)).getRegisterA() == 9
                            && code.get(247) instanceof ReferenceInstruction);
            var field = ((ReferenceInstruction) code.get(247)).getReference();
            need(
                    field.toString()
                            .equals(
                                    "Lcom/qinggan/launcher/base/utils/AppLauncher;->TAG:Ljava/lang/String;"));
            var service = b.newLabelForIndex(234);
            b.replaceInstruction(
                    247,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_STATIC,
                            1,
                            2,
                            0,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    HELPER, "useService", List.of("Ljava/lang/Object;"), "Z")));
            b.addInstruction(248, new BuilderInstruction11x(Opcode.MOVE_RESULT, 0));
            b.addInstruction(249, new BuilderInstruction21t(Opcode.IF_NEZ, 0, service));
            b.addInstruction(250, new BuilderInstruction21c(Opcode.SGET_OBJECT, 9, field));

            return new ImmutableMethod(
                    m.getDefiningClass(),
                    m.getName(),
                    m.getParameters(),
                    m.getReturnType(),
                    m.getAccessFlags(),
                    m.getAnnotations(),
                    m.getHiddenApiRestrictions(),
                    b);
        }

        static byte[] apply(
                Map<String, Object> constants, int api, String abi, Path apk, byte[] raw)
                throws Exception {
            validate(constants, api, abi, apk, raw);
            var before = classes(raw);
            var out = new TreeMap<String, ClassDef>();

            for (var c : before.values()) {
                if (c.getType().equals("Lcom/qinggan/launcher/base/utils/AppLauncher;")) {
                    var direct = new ArrayList<Method>();

                    for (var m : c.getDirectMethods()) {
                        direct.add(m.getName().equals("startAppBySubType") ? adaptRoute(m) : m);
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
                                    c.getVirtualMethods()));
                    continue;
                }

                if (!c.getType().equals(OWNER)) {
                    out.put(c.getType(), c);
                    continue;
                }

                var direct = new ArrayList<Method>();
                var virtual = new ArrayList<Method>();

                for (var m : c.getDirectMethods()) {
                    direct.add(METHODS.contains(m.getName()) ? adapt(m) : m);
                }

                for (var m : c.getVirtualMethods()) {
                    virtual.add(METHODS.contains(m.getName()) ? adapt(m) : m);
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
            need(before.keySet().equals(reread.keySet()));

            for (var e : out.entrySet()) {
                need(
                        FrameworkDexMaterializer.semantic(e.getValue())
                                .equals(FrameworkDexMaterializer.semantic(reread.get(e.getKey()))));
            }

            return result;
        }
    }
}
