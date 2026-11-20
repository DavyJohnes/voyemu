package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.formats.Instruction35c;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction11n;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction11x;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;

// Unsupported OEM backlight operations report false at the MEGA compatibility boundary.
// No vehicle/power state is invented and no cleanbase PowerManager class is changed.
final class PassionPowerAdapter {
    static final String NAME = "passion-api30-power";
    static final String PIN = "ec2933b1c42f1932cd46117a475b84d83371c9b19130930f43ffd4c455ca6bd3";
    static final String OWNER = "Lcom/mega/nexus/os/MegaPowerManagerAdapter;";

    private static void require(boolean value) {
        Failure.require(
                value,
                "PASSION_POWER_CONTRACT",
                "Exact owner-pinned Passion API30 power boundary required",
                3);
    }

    private static Method unsupported(Method method) {
        boolean all = method.getName().equals("updateAllBackLightState");
        List<String> parameters =
                all
                        ? List.of("Landroid/os/PowerManager;", "I", "I")
                        : List.of("Landroid/os/PowerManager;", "Ljava/lang/String;", "I", "I");

        require(
                method.getAccessFlags() == 9
                        && method.getReturnType().equals("Z")
                        && method.getParameterTypes().equals(parameters)
                        && method.getImplementation() != null);
        var body = method.getImplementation();
        require(body.getRegisterCount() == parameters.size() + 1 && body.getTryBlocks().isEmpty());
        List<Instruction> instructions = new ArrayList<>();
        body.getInstructions().forEach(instructions::add);
        require(instructions.size() == 3);
        require(
                instructions.get(0) instanceof Instruction35c
                        && instructions.get(0).getOpcode() == Opcode.INVOKE_VIRTUAL);
        var call = (Instruction35c) instructions.get(0);
        require(
                call.getRegisterCount() == parameters.size()
                        && call.getRegisterC() == 1
                        && call.getRegisterD() == 2
                        && call.getRegisterE() == 3
                        && call.getRegisterF() == (all ? 0 : 4)
                        && call.getRegisterG() == 0);
        require(
                call.getReference()
                        .toString()
                        .equals(
                                "Landroid/os/PowerManager;->"
                                        + method.getName()
                                        + (all ? "(II)Z" : "(Ljava/lang/String;II)Z")));
        require(
                instructions.get(1).getOpcode() == Opcode.MOVE_RESULT
                        && instructions.get(1) instanceof OneRegisterInstruction
                        && ((OneRegisterInstruction) instructions.get(1)).getRegisterA() == 0);
        require(
                instructions.get(2).getOpcode() == Opcode.RETURN
                        && instructions.get(2) instanceof OneRegisterInstruction
                        && ((OneRegisterInstruction) instructions.get(2)).getRegisterA() == 0);
        var own =
                new ImmutableMethodImplementation(
                        body.getRegisterCount(),
                        List.of(
                                new ImmutableInstruction11n(Opcode.CONST_4, 0, 0),
                                new ImmutableInstruction11x(Opcode.RETURN, 0)),
                        List.of(),
                        List.of());

        return new ImmutableMethod(
                method.getDefiningClass(),
                method.getName(),
                method.getParameters(),
                method.getReturnType(),
                method.getAccessFlags(),
                method.getAnnotations(),
                method.getHiddenApiRestrictions(),
                own);
    }

    static byte[] apply(String profile, int api, Path original, byte[] donor) throws Exception {
        require(api == 30 && FilesSupport.hash(original).equals(PIN));
        ClassDef source = null;

        for (ClassDef type : DexOperations.dex(original, "classes4.dex", 30).getClasses()) {
            if (type.getType().equals(OWNER)) {
                require(source == null);
                source = type;
            }
        }

        require(source != null);
        var classes = new TreeMap<String, ClassDef>();

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(30), donor).getClasses()) {
            require(classes.put(type.getType(), type) == null);
        }

        ClassDef selected = classes.get(OWNER);
        require(
                selected != null
                        && FrameworkDexMaterializer.semantic(source)
                                .equals(FrameworkDexMaterializer.semantic(selected)));
        List<Method> direct = new ArrayList<>();
        int replaced = 0;

        for (Method method : selected.getDirectMethods()) {
            if (method.getName().equals("updateAllBackLightState")
                    || method.getName().equals("updateBackLightState")) {
                direct.add(unsupported(method));
                ++replaced;
            } else {
                direct.add(method);
            }
        }

        require(replaced == 2);
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
                        direct,
                        selected.getVirtualMethods()));
        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), classes.values()));
        var reread = new TreeMap<String, ClassDef>();

        for (ClassDef type : new DexBackedDexFile(Opcodes.forApi(30), output).getClasses()) {
            reread.put(type.getType(), type);
        }

        require(reread.keySet().equals(classes.keySet()));

        for (var entry : classes.entrySet()) {
            require(
                    FrameworkDexMaterializer.semantic(entry.getValue())
                            .equals(FrameworkDexMaterializer.semantic(reread.get(entry.getKey()))));
        }

        require(FilesSupport.hash(original).equals(PIN));

        return output;
    }
}
