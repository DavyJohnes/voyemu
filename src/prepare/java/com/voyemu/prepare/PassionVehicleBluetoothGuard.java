package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.MethodImplementation;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.WideLiteralInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction31i;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * Clean API30 has no OEM role extension: no listener callbacks; query retains OEM AV_INVALID_ROLE
 * (-1), role switching returns unsupported false.
 */
final class PassionVehicleBluetoothGuard {
    static final String NAME = "passion-api30-vehicle-bluetooth";
    static final String PIN = "a5fb5daed4a295a568daf0390858a0bd00782b1384e66b13a96f66d43ad43e60";
    static final String OWNER = "Lcom/mega/nexus/bluetooth/MegaBluetoothAdapter;";
    static final List<String> METHODS =
            List.of(
                    "addRoleSwitchListener",
                    "removeRoleSwitchListener",
                    "getCurrentAvRole",
                    "switchAvRole");

    private static void require(boolean value) {
        Failure.require(
                value,
                "PASSION_VEHICLE_BT_CONTRACT",
                "Exact owner-pinned Passion optional Bluetooth listener required",
                3);
    }

    static Map<String, Method> methods(DexFile dex) {
        var found = new TreeMap<String, Method>();

        for (var c : dex.getClasses()) {
            if (c.getType().equals(OWNER)) {
                for (var m : c.getMethods()) {
                    if (METHODS.contains(m.getName())) {
                        require(found.put(m.getName(), m) == null);
                        validateMethod(m);
                    }
                }
            }
        }

        require(found.size() == 4);

        return found;
    }

    static void validateMethod(Method m) {
        boolean query = m.getName().equals("getCurrentAvRole"),
                roleSwitch = m.getName().equals("switchAvRole"),
                value = query || roleSwitch;

        requireSignature(m, query, roleSwitch);
        var b = m.getImplementation();
        require(b != null && b.getRegisterCount() == (query ? 1 : 2) && b.getTryBlocks().isEmpty());
        int at = 0, checked = 0;

        for (var i : b.getInstructions()) {
            if (at == 0) {
                requireAdapterCall(i, m.getName(), query, roleSwitch);
                checked++;
            }

            if (at == 3) {
                require(
                        i.getOpcode() == (value ? Opcode.MOVE_RESULT : Opcode.RETURN_VOID)
                                && (!value || ((OneRegisterInstruction) i).getRegisterA() == 0));
                checked++;
            }

            if (value && at == 4) {
                require(
                        i.getOpcode() == Opcode.RETURN
                                && ((OneRegisterInstruction) i).getRegisterA() == 0);
                checked++;
            }

            at += i.getCodeUnits();
        }

        require(at == (value ? 5 : 4) && checked == (value ? 3 : 2));
    }

    private static void requireSignature(Method m, boolean query, boolean roleSwitch) {
        require(
                METHODS.contains(m.getName())
                        && m.getAccessFlags() == 9
                        && m.getReturnType().equals(query ? "I" : roleSwitch ? "Z" : "V")
                        && m.getParameterTypes()
                                .equals(
                                        query
                                                ? List.of("Landroid/bluetooth/BluetoothAdapter;")
                                                : roleSwitch
                                                        ? List.of(
                                                                "Landroid/bluetooth/BluetoothAdapter;",
                                                                "I")
                                                        : List.of(
                                                                "Landroid/bluetooth/BluetoothAdapter;",
                                                                "Lcom/mega/nexus/bluetooth/MegaBluetoothAdapter$RoleSwitchListener;")));
    }

    private static void requireAdapterCall(
            Instruction i, String name, boolean query, boolean roleSwitch) {
        require(
                i.getOpcode() == Opcode.INVOKE_VIRTUAL
                        && i instanceof FiveRegisterInstruction r
                        && r.getRegisterCount() == (query ? 1 : 2)
                        && r.getRegisterC() == 0
                        && (query || r.getRegisterD() == 1)
                        && ((ReferenceInstruction) i)
                                .getReference()
                                .toString()
                                .equals(
                                        "Landroid/bluetooth/BluetoothAdapter;->"
                                                + name
                                                + (query
                                                        ? "()I"
                                                        : roleSwitch
                                                                ? "(I)Z"
                                                                : "(Landroid/bluetooth/BluetoothAdapter$RoleSwitchListener;)V")));
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        require(api == 30 && FilesSupport.hash(original).equals(PIN) && dex != null);
        methods(new DexBackedDexFile(Opcodes.forApi(30), dex));
    }

    static ImmutableDexFile rewrite(DexFile source, Map<String, Method> original, boolean inverse) {
        var classes = new ArrayList<ClassDef>();

        for (var c : source.getClasses()) {
            if (!c.getType().equals(OWNER)) {
                classes.add(c);
                continue;
            }

            var direct = new ArrayList<Method>();

            for (var m : c.getDirectMethods()) {
                if (METHODS.contains(m.getName())) {
                    m = routedMethod(m, original.get(m.getName()), inverse);
                }

                direct.add(m);
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
                            c.getVirtualMethods()));
        }

        return new ImmutableDexFile(Opcodes.forApi(30), classes);
    }

    private static Method routedMethod(Method m, Method original, boolean inverse) {
        var b = m.getImplementation();
        boolean query = m.getName().equals("getCurrentAvRole"),
                value = query || m.getName().equals("switchAvRole");
        var originals = new HashMap<Integer, Instruction>();
        int address = 0;

        for (var i : original.getImplementation().getInstructions()) {
            originals.put(address, i);
            address += i.getCodeUnits();
        }

        var list =
                value
                        ? routedValueInstructions(b, originals, query, inverse)
                        : routedListenerInstructions(b, originals, inverse);

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                new ImmutableMethodImplementation(
                        b.getRegisterCount(), list, b.getTryBlocks(), b.getDebugItems()));
    }

    private static List<Instruction> routedValueInstructions(
            MethodImplementation b,
            Map<Integer, Instruction> originals,
            boolean query,
            boolean inverse) {
        var list = new ArrayList<Instruction>();
        int at = 0, changed = 0;

        for (var i : b.getInstructions()) {
            if (at == 0 || at == 3) {
                if (inverse) {
                    require(
                            at == 0
                                    ? i.getOpcode() == Opcode.CONST
                                            && ((OneRegisterInstruction) i).getRegisterA() == 0
                                            && ((WideLiteralInstruction) i).getWideLiteral()
                                                    == (query ? -1 : 0)
                                    : i.getOpcode() == Opcode.NOP);
                    list.add(originals.get(at));
                } else {
                    list.add(
                            at == 0
                                    ? new ImmutableInstruction31i(Opcode.CONST, 0, query ? -1 : 0)
                                    : new ImmutableInstruction10x(Opcode.NOP));
                }

                changed++;
            } else {
                list.add(i);
            }

            at += i.getCodeUnits();
        }

        require(at == 5 && changed == 2);

        return list;
    }

    private static List<Instruction> routedListenerInstructions(
            MethodImplementation b, Map<Integer, Instruction> originals, boolean inverse) {
        var list = new ArrayList<Instruction>();
        int at = 0, changed = 0;

        for (var i : b.getInstructions()) {
            if (at < 3) {
                if (inverse) {
                    require(i.getOpcode() == Opcode.NOP);

                    if (at == 0) {
                        list.add(originals.get(0));
                    }

                    changed++;
                } else {
                    require(at == 0 && i.getOpcode() == Opcode.INVOKE_VIRTUAL);

                    for (int n = 0; n < 3; n++) {
                        list.add(new ImmutableInstruction10x(Opcode.NOP));
                    }

                    changed++;
                }
            } else {
                list.add(i);
            }

            at += i.getCodeUnits();
        }

        require(at == 4 && changed == (inverse ? 3 : 1));

        return list;
    }

    static void validateSettings(String profile, int api, Path original, byte[] dex)
            throws Exception {
        require(api == 30 && FilesSupport.hash(original).equals(PIN) && dex != null);
        PassionBluetoothRoleGuard.validateMethod(
                PassionBluetoothRoleGuard.method(new DexBackedDexFile(Opcodes.forApi(30), dex)));
    }

    static byte[] applySettings(String profile, int api, Path original, byte[] dex)
            throws Exception {
        validateSettings(profile, api, original, dex);
        var before = new DexBackedDexFile(Opcodes.forApi(30), dex);
        var method = PassionBluetoothRoleGuard.method(before);
        byte[] result =
                FrameworkDexMaterializer.serialize(
                        PassionBluetoothRoleGuard.rewrite(before, method, false));
        var normalized =
                PassionBluetoothRoleGuard.rewrite(
                        new DexBackedDexFile(Opcodes.forApi(30), result), method, true);
        var types = new HashMap<String, ClassDef>();

        for (var c : normalized.getClasses()) {
            types.put(c.getType(), c);
        }

        require(types.size() == before.getClasses().size());

        for (var c : before.getClasses()) {
            require(
                    FrameworkDexMaterializer.semantic(c)
                            .equals(FrameworkDexMaterializer.semantic(types.get(c.getType()))));
        }

        return result;
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        validate(profile, api, original, dex);
        var before = new DexBackedDexFile(Opcodes.forApi(30), dex);
        var methods = methods(before);
        byte[] result = FrameworkDexMaterializer.serialize(rewrite(before, methods, false));
        var normalized = rewrite(new DexBackedDexFile(Opcodes.forApi(30), result), methods, true);
        var types = new HashMap<String, ClassDef>();

        for (var c : normalized.getClasses()) {
            types.put(c.getType(), c);
        }

        require(types.size() == before.getClasses().size());

        for (var c : before.getClasses()) {
            require(
                    FrameworkDexMaterializer.semantic(c)
                            .equals(FrameworkDexMaterializer.semantic(types.get(c.getType()))));
        }

        return result;
    }
}
