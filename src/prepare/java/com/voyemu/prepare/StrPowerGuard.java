package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction22c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableTypeReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

// A desktop without automotive CarService has no STR power lifecycle; preserve the genuine typed
// path.
final class StrPowerGuard {
    static final String NAME = "api30-str-power";
    static final String OWNER = "Lcom/qinggan/setting/str/StrStateManager;",
            MANAGER = "Landroid/car/hardware/power/CarPowerManager;";

    private static void require(boolean value) {
        Failure.require(
                value,
                "STR_POWER_CONTRACT",
                "Exact Free A11 7.3.1 API30 unsupported STR power boundary required",
                3);
    }

    private static boolean target(Method method) {
        return method.getName().equals("init")
                && method.getParameterTypes().equals(List.of("Landroid/content/Context;"))
                && method.getReturnType().equals("V");
    }

    private static Method method(byte[] dex) {
        Method selected = null;

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            if (type.getType().equals(OWNER)) {
                for (Method candidate : type.getVirtualMethods()) {
                    if (target(candidate)) {
                        require(selected == null);
                        selected = candidate;
                    }
                }
            }
        }

        require(
                selected != null
                        && selected.getAccessFlags() == 1
                        && selected.getImplementation() != null
                        && selected.getImplementation().getRegisterCount() == 6
                        && selected.getImplementation().getTryBlocks().isEmpty());

        return selected;
    }

    static void validate(Map<String, Object> constants, int api, Path original, byte[] dex)
            throws Exception {
        require(api == 30 && StepConstants.admits(constants, FilesSupport.hash(original)));
        require(dex != null);
        method(dex);
    }

    private static Method guarded(Method method) {
        var body = new MutableMethodImplementation(method.getImplementation());
        var unsupported = body.newLabelForAddress(0x1c);
        int address = 0;
        var indexes = new ArrayList<Integer>();

        for (int index = 0; index < body.getInstructions().size(); index++) {
            if (address == 0xc || address == 0x12) {
                indexes.add(index);
            }

            address += body.getInstructions().get(index).getCodeUnits();
        }

        require(indexes.size() == 2);
        body.addInstruction(
                indexes.get(1),
                new BuilderInstruction22c(
                        Opcode.INSTANCE_OF, 0, 5, new ImmutableTypeReference(MANAGER)));
        body.addInstruction(
                indexes.get(1) + 1, new BuilderInstruction21t(Opcode.IF_EQZ, 0, unsupported));
        body.addInstruction(
                indexes.get(0), new BuilderInstruction21t(Opcode.IF_EQZ, 5, unsupported));

        return new ImmutableMethod(
                method.getDefiningClass(),
                method.getName(),
                method.getParameters(),
                method.getReturnType(),
                method.getAccessFlags(),
                method.getAnnotations(),
                method.getHiddenApiRestrictions(),
                body);
    }

    static byte[] apply(Map<String, Object> constants, int api, Path original, byte[] dex)
            throws Exception {
        validate(constants, api, original, dex);
        var classes = new TreeMap<String, ClassDef>();

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            require(classes.put(type.getType(), type) == null);
        }

        ClassDef selected = classes.get(OWNER);
        require(selected != null);
        var virtual = new ArrayList<Method>();
        int replaced = 0;

        for (Method candidate : selected.getVirtualMethods()) {
            if (target(candidate)) {
                virtual.add(guarded(candidate));
                replaced++;
            } else {
                virtual.add(candidate);
            }
        }

        require(replaced == 1);
        classes.put(
                OWNER,
                new ImmutableClassDef(
                        selected.getType(),
                        selected.getAccessFlags(),
                        selected.getSuperclass(),
                        selected.getInterfaces(),
                        selected.getSourceFile(),
                        selected.getAnnotations(),
                        selected.getStaticFields(),
                        selected.getInstanceFields(),
                        selected.getDirectMethods(),
                        virtual));
        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), classes.values()));
        var reread = new TreeMap<String, ClassDef>();

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(30), output).getClasses()) {
            require(reread.put(type.getType(), type) == null);
        }

        require(reread.keySet().equals(classes.keySet()));

        for (var entry : classes.entrySet()) {
            if (!FrameworkDexMaterializer.semantic(entry.getValue())
                    .equals(FrameworkDexMaterializer.semantic(reread.get(entry.getKey())))) {
                throw new Failure(
                        "STR_POWER_CONTRACT",
                        "Materialized owner definition changed: "
                                + entry.getKey()
                                + " "
                                + FrameworkDexMaterializer.difference(
                                        entry.getValue(), reread.get(entry.getKey())),
                        3);
            }
        }

        require(StepConstants.admits(constants, FilesSupport.hash(original)));

        return output;
    }
}
