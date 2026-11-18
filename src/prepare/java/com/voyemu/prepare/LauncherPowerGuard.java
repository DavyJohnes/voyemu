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
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableStringReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

// A desktop without automotive CarService has no Launcher power lifecycle; preserve the genuine
// typed path.
final class LauncherPowerGuard {
    static final String NAME = "api30-launcher-power";

    /** Pin of the Launcher input; boundary selects the instruction-boundary check. */
    record Variant(String pin, boolean boundary) {}

    static final Map<String, Variant> VARIANTS =
            Map.of(
                    "free-11-chinese-20250710-6.11.1",
                    new Variant(
                            "48665cf9d6bb50b49b3aa0b1aa73bdb0ac5d9d7071240c46c7f13d07d4114d7e",
                            true),
                    "free-11-chinese-20260731-7.3.1",
                    new Variant(
                            "b3287150827e7fa51072bcbcba257539101cda8433c5f9d3a95f4ca905abd567",
                            false));

    static List<String> pins(String profile) {
        var variant = VARIANTS.get(profile);

        return variant == null ? List.of() : List.of(variant.pin());
    }

    static final String MAP_OWNER = "Lcom/baidu/widgetmap/MapWidgetManager;",
            POWER_OWNER = "Lcom/qinggan/app/launcher/manager/PowerStateManagerWrapper;";

    static boolean wrapper(String entry) {
        require(entry.equals("classes.dex") || entry.equals("classes2.dex"));

        return entry.equals("classes2.dex");
    }

    static String owner(String entry) {
        return wrapper(entry) ? POWER_OWNER : MAP_OWNER;
    }

    private static void require(boolean value) {
        Failure.require(
                value,
                "LAUNCHER_POWER_CONTRACT",
                "Exact API30 unsupported Launcher power boundary required",
                3);
    }

    private static boolean target(Method method, String entry) {
        return method.getName().equals(wrapper(entry) ? "<init>" : "init")
                && method.getParameterTypes().isEmpty()
                && method.getReturnType().equals("V");
    }

    private static Method method(byte[] dex, String entry) {
        Method selected = null;

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            if (type.getType().equals(owner(entry))) {
                for (Method candidate : type.getMethods()) {
                    if (target(candidate, entry)) {
                        require(selected == null);
                        selected = candidate;
                    }
                }
            }
        }

        require(
                selected != null
                        && selected.getAccessFlags() == (wrapper(entry) ? 65538 : 1)
                        && selected.getImplementation() != null
                        && selected.getImplementation().getRegisterCount() == 6
                        && selected.getImplementation().getTryBlocks().isEmpty());

        return selected;
    }

    static void validate(String profile, int api, Path original, byte[] dex, String entry)
            throws Exception {
        var variant = VARIANTS.get(profile);

        require(variant != null && api == 30 && FilesSupport.hash(original).equals(variant.pin()));
        require(dex != null);
        Method selected = method(dex, entry);

        if (variant.boundary()) {
            int address = 0;
            boolean insertion = false, exit = false;

            for (var instruction : selected.getImplementation().getInstructions()) {
                if (address == (wrapper(entry) ? 0xa : 0x1d)) {
                    require(
                            instruction
                                    instanceof
                                    org.jf.dexlib2.iface.instruction.ReferenceInstruction);
                    var ref =
                            ((org.jf.dexlib2.iface.instruction.ReferenceInstruction) instruction)
                                    .getReference()
                                    .toString();

                    if (wrapper(entry)) {
                        require(
                                instruction.getOpcode() == Opcode.INVOKE_STATIC
                                        && ref.equals(
                                                "Lcom/qinggan/launcher/base/BaseApplication;->getContext()Lcom/qinggan/launcher/base/BaseApplication;"));
                    } else {
                        require(
                                instruction.getOpcode() == Opcode.IGET_OBJECT
                                        && ref.equals(
                                                "Lcom/baidu/widgetmap/MapWidgetManager;->mContext:Landroid/content/Context;")
                                        && ((org.jf.dexlib2.iface.instruction
                                                                        .TwoRegisterInstruction)
                                                                instruction)
                                                        .getRegisterA()
                                                == 0
                                        && ((org.jf.dexlib2.iface.instruction
                                                                        .TwoRegisterInstruction)
                                                                instruction)
                                                        .getRegisterB()
                                                == 5);
                    }

                    insertion = true;
                }

                if (address == (wrapper(entry) ? 0x19 : 0x3d)) {
                    require(instruction.getOpcode() == Opcode.RETURN_VOID);
                    exit = true;
                }

                address += instruction.getCodeUnits();
            }

            require(insertion && exit);
        }
    }

    private static Method guarded(Method method, String entry) {
        var body = new MutableMethodImplementation(method.getImplementation());
        var unsupported = body.newLabelForAddress(wrapper(entry) ? 0x19 : 0x3d);
        int address = 0, index = -1;

        for (int i = 0; i < body.getInstructions().size(); i++) {
            if (address == (wrapper(entry) ? 0xa : 0x1d)) {
                index = i;
            }

            address += body.getInstructions().get(i).getCodeUnits();
        }

        require(index >= 0);
        body.addInstruction(
                index++,
                new BuilderInstruction21c(
                        Opcode.CONST_STRING, 0, new ImmutableStringReference("car_service")));
        body.addInstruction(
                index++,
                new BuilderInstruction35c(
                        Opcode.INVOKE_STATIC,
                        1,
                        0,
                        0,
                        0,
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Landroid/os/ServiceManager;",
                                "checkService",
                                List.of("Ljava/lang/String;"),
                                "Landroid/os/IBinder;")));
        body.addInstruction(index++, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0));
        body.addInstruction(index, new BuilderInstruction21t(Opcode.IF_EQZ, 0, unsupported));

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

    static byte[] apply(String profile, int api, Path original, byte[] dex, String entry)
            throws Exception {
        validate(profile, api, original, dex, entry);
        var classes = new TreeMap<String, ClassDef>();

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            require(classes.put(type.getType(), type) == null);
        }

        ClassDef selected = classes.get(owner(entry));
        require(selected != null);
        var virtual = new ArrayList<Method>();
        var direct = new ArrayList<Method>();
        int replaced = 0;

        for (Method candidate : selected.getVirtualMethods()) {
            if (target(candidate, entry)) {
                virtual.add(guarded(candidate, entry));
                replaced++;
            } else {
                virtual.add(candidate);
            }
        }

        for (Method candidate : selected.getDirectMethods()) {
            if (target(candidate, entry)) {
                direct.add(guarded(candidate, entry));
                replaced++;
            } else {
                direct.add(candidate);
            }
        }

        require(replaced == 1);
        classes.put(
                owner(entry),
                new ImmutableClassDef(
                        selected.getType(),
                        selected.getAccessFlags(),
                        selected.getSuperclass(),
                        selected.getInterfaces(),
                        selected.getSourceFile(),
                        selected.getAnnotations(),
                        selected.getStaticFields(),
                        selected.getInstanceFields(),
                        direct,
                        virtual));
        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), classes.values()));
        var reread = new TreeMap<String, ClassDef>();

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(30), output).getClasses()) {
            require(reread.put(type.getType(), type) == null);
        }

        require(reread.keySet().equals(classes.keySet()));

        for (var definition : classes.entrySet()) {
            if (!FrameworkDexMaterializer.semantic(definition.getValue())
                    .equals(FrameworkDexMaterializer.semantic(reread.get(definition.getKey())))) {
                throw new Failure(
                        "LAUNCHER_POWER_CONTRACT",
                        "Materialized owner definition changed: "
                                + definition.getKey()
                                + " "
                                + FrameworkDexMaterializer.difference(
                                        definition.getValue(), reread.get(definition.getKey())),
                        3);
            }
        }

        require(FilesSupport.hash(original).equals(VARIANTS.get(profile).pin()));

        return output;
    }
}
