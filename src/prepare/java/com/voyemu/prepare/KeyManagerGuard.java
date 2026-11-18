package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction35c;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Numeric/reference-only adaptation of one exact Free OEM KeyManager; no donor implementation. */
final class KeyManagerGuard {
    static final String NAME = "api30-keymanager";

    /** Pin of the KeyManager input and the shared libraries its recipe declares. */
    record Variant(String pin, List<String> libraries) {}

    static final Map<String, Variant> VARIANTS =
            Map.of(
                    "free-11-chinese-20250710-6.11.1",
                    new Variant(
                            "511eaafefe567e5c08f5683ba86aab043dffbfc21efab9e00cdf4cd8caf67505",
                            List.of("QGAPI", "QGMOD", "VoyemuFreeWheel")),
                    "free-11-chinese-20260731-7.3.1",
                    new Variant(
                            "951abb4f86540663fd654138b7e58cd0226d5dfa0470a4f5e85384bcdedbb78e",
                            List.of("QGAPI", "QGMOD", "QGVehicle", "VoyemuFreeWheel")));

    static List<String> pins(String profile) {
        var variant = VARIANTS.get(profile);

        return variant == null ? List.of() : List.of(variant.pin());
    }

    static List<String> libraries(String profile) {
        var variant = VARIANTS.get(profile);

        return variant == null ? List.of() : variant.libraries();
    }

    static final String VOLUME = "Lcom/qinggan/keymanager/service/sinks/KtVolumeAdjust;";
    static final String READER = "Lcom/qinggan/keymanager/service/engine/KeyManagerReader;";
    private static final String POLICY = "Lcom/qinggan/audiopolicy/AudioPolicyManager;";
    private static final String CONTEXT = "Landroid/content/Context;",
            ACTIONS = "Lcom/voyemu/input/FreeWheelMediaActions;";

    private record Site(
            ImmutableMethodReference before,
            ImmutableMethodReference after,
            Opcode opcode,
            int count) {}

    private static ImmutableMethodReference ref(
            String owner, String name, String result, String... args) {
        return new ImmutableMethodReference(owner, name, List.of(args), result);
    }

    private static final Site CONTEXT_SITE =
            new Site(
                    ref(VOLUME, "getMAudioPolicyManager", POLICY),
                    ref(VOLUME, "getMContext", CONTEXT),
                    Opcode.INVOKE_VIRTUAL,
                    1);
    private static final Site ADJUST_SITE =
            new Site(
                    ref(POLICY, "adjustVolume", "V", "I", "I"),
                    ref(ACTIONS, "adjustVolume", "V", CONTEXT, "I", "I"),
                    Opcode.INVOKE_STATIC,
                    3);
    private static final Site QUERY_SITE =
            new Site(
                    ref(POLICY, "isMasterMute", "Z"),
                    ref(ACTIONS, "isMediaMuted", "Z", CONTEXT),
                    Opcode.INVOKE_STATIC,
                    1);
    private static final Site MUTE_SITE =
            new Site(
                    ref(POLICY, "setMasterMute", "V", "Z", "I"),
                    ref(ACTIONS, "setMediaMuted", "V", CONTEXT, "Z", "I"),
                    Opcode.INVOKE_STATIC,
                    3);
    private static final Site CANCEL_SITE =
            new Site(
                    ref(
                            READER.substring(0, READER.length() - 1) + "$Processor;",
                            "keyOut",
                            "V",
                            "Lcom/qinggan/system/QGKeyEvent;"),
                    ref(
                            "Lcom/voyemu/input/FreeWheelCancellation;",
                            "keyOut",
                            "V",
                            "Ljava/lang/Object;",
                            "Landroid/view/KeyEvent;"),
                    Opcode.INVOKE_STATIC,
                    2);

    private static void require(boolean value) {
        Failure.require(
                value,
                "KEYMANAGER_CONTRACT",
                "Exact Free API30 KeyManager media/cancel contract required",
                3);
    }

    private static Map<Integer, Site> sites(Method method) {
        if (method.getDefiningClass().equals(VOLUME)
                && method.getName().equals("adjustVol")
                && method.getParameterTypes().equals(List.of("I", "I"))) {
            return Map.of(46, CONTEXT_SITE, 50, ADJUST_SITE);
        }

        if (method.getDefiningClass().equals(VOLUME)
                && method.getName().equals("mute")
                && method.getParameterTypes().isEmpty()) {
            return Map.of(
                    12,
                    CONTEXT_SITE,
                    16,
                    QUERY_SITE,
                    81,
                    CONTEXT_SITE,
                    85,
                    CONTEXT_SITE,
                    89,
                    QUERY_SITE,
                    96,
                    MUTE_SITE);
        }

        if (method.getDefiningClass().equals(READER)
                && method.getName().equals("onKeyEvent")
                && method.getParameterTypes().equals(List.of("Landroid/view/KeyEvent;"))) {
            return Map.of(117, CANCEL_SITE);
        }

        return Map.of();
    }

    private static void shape(Method method) {
        var body = method.getImplementation();

        require(body != null);
        int units = 0;

        for (Instruction instruction : body.getInstructions()) {
            units += instruction.getCodeUnits();
        }

        if (method.getName().equals("adjustVol")) {
            require(
                    body.getRegisterCount() == 8
                            && units == 96
                            && method.getReturnType().equals("V"));
        } else if (method.getName().equals("mute")) {
            require(
                    body.getRegisterCount() == 5
                            && units == 124
                            && method.getReturnType().equals("V"));
        } else {
            require(
                    body.getRegisterCount() == 7
                            && units == 168
                            && method.getReturnType().equals("Z"));
        }
    }

    private static Method rewrite(Method method, boolean inverse, int[] changed) {
        Map<Integer, Site> sites = sites(method);

        if (sites.isEmpty()) {
            return method;
        }

        shape(method);
        var body = method.getImplementation();
        var output = new ArrayList<Instruction>();
        int address = 0, found = 0;

        for (Instruction instruction : body.getInstructions()) {
            Site site = sites.get(address);

            if (site != null) {
                require(
                        instruction instanceof FiveRegisterInstruction
                                && instruction instanceof ReferenceInstruction referenceInstruction
                                && instruction.getOpcode()
                                        == (inverse ? site.opcode : Opcode.INVOKE_VIRTUAL)
                                && referenceInstruction
                                        .getReference()
                                        .equals(inverse ? site.after : site.before));
                FiveRegisterInstruction registers = (FiveRegisterInstruction) instruction;
                require(registers.getRegisterCount() == site.count);
                output.add(
                        new ImmutableInstruction35c(
                                inverse ? Opcode.INVOKE_VIRTUAL : site.opcode,
                                registers.getRegisterCount(),
                                registers.getRegisterC(),
                                registers.getRegisterD(),
                                registers.getRegisterE(),
                                registers.getRegisterF(),
                                registers.getRegisterG(),
                                inverse ? site.before : site.after));
                found++;
                changed[0]++;
            } else {
                output.add(instruction);
            }

            address += instruction.getCodeUnits();
        }

        require(found == sites.size());

        return new ImmutableMethod(
                method.getDefiningClass(),
                method.getName(),
                method.getParameters(),
                method.getReturnType(),
                method.getAccessFlags(),
                method.getAnnotations(),
                method.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(
                        body.getRegisterCount(),
                        output,
                        body.getTryBlocks(),
                        body.getDebugItems()));
    }

    private static ImmutableDexFile rewrite(DexFile source, boolean inverse) {
        var output = new TreeMap<String, ClassDef>();
        int[] changed = {0};

        for (ClassDef type : source.getClasses()) {
            if (!type.getType().equals(VOLUME) && !type.getType().equals(READER)) {
                output.put(type.getType(), type);
                continue;
            }

            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();

            for (Method method : type.getDirectMethods()) {
                direct.add(rewrite(method, inverse, changed));
            }

            for (Method method : type.getVirtualMethods()) {
                virtual.add(rewrite(method, inverse, changed));
            }

            output.put(
                    type.getType(),
                    new ImmutableClassDef(
                            type.getType(),
                            type.getAccessFlags(),
                            type.getSuperclass(),
                            type.getInterfaces(),
                            type.getSourceFile(),
                            type.getAnnotations(),
                            type.getStaticFields(),
                            type.getInstanceFields(),
                            direct,
                            virtual));
        }

        require(changed[0] == 9);

        return new ImmutableDexFile(Opcodes.forApi(30), output.values());
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        require(
                VARIANTS.containsKey(profile)
                        && api == 30
                        && original != null
                        && FilesSupport.hash(original).equals(VARIANTS.get(profile).pin())
                        && dex != null);
        var source = new DexBackedDexFile(Opcodes.forApi(30), dex);
        int fields = 0;

        for (ClassDef type : source.getClasses()) {
            for (var field : type.getInstanceFields()) {
                if (type.getType().equals(READER.substring(0, READER.length() - 1) + "$Processor;")
                        && field.getName().equals("mProcesses")) {
                    require(field.getType().equals("Ljava/util/Map;"));
                    fields++;
                }

                if (type.getType().equals(READER.substring(0, READER.length() - 1) + "$KeyProcess;")
                        && field.getName().equals("phase")) {
                    require(field.getType().equals("I"));
                    fields++;
                }
            }
        }

        require(fields == 2);
        rewrite(source, false);
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        validate(profile, api, original, dex);
        var before = new DexBackedDexFile(Opcodes.forApi(30), dex);
        byte[] bytes = FrameworkDexMaterializer.serialize(rewrite(before, false));
        var normalized = rewrite(new DexBackedDexFile(Opcodes.forApi(30), bytes), true);
        var byName = new TreeMap<String, ClassDef>();

        for (ClassDef type : normalized.getClasses()) {
            byName.put(type.getType(), type);
        }

        require(byName.size() == before.getClasses().size());

        for (ClassDef type : before.getClasses()) {
            require(
                    byName.containsKey(type.getType())
                            && FrameworkDexMaterializer.semantic(type)
                                    .equals(
                                            FrameworkDexMaterializer.semantic(
                                                    byName.get(type.getType()))));
        }

        return bytes;
    }
}
