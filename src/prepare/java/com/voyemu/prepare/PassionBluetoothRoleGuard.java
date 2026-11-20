package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
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
import java.util.Set;

/** The clean API30 stack does not offer the owner's optional Bluetooth role-switch extension. */
final class PassionBluetoothRoleGuard {
    static final String NAME = "passion-api30-bluetooth-role";
    static final Set<String> PINS =
            Set.of(
                    "a3c788860d5df27277d53ffbf838daaefddb6ab098ad52b636671dd70b34765d",
                    "5bc0f97b94009b2e5aa6cb9759ea2547ab17cdadfbb8021d5e9cb6d6c6589047");
    static final String OWNER = "Lcom/android/settingslib/bluetooth/LocalBluetoothProfileManager;";

    static void require(boolean value) {
        Failure.require(
                value,
                "PASSION_BT_ROLE_CONTRACT",
                "Exact owner-pinned Passion optional Bluetooth role query required",
                3);
    }

    static Method method(DexFile dex) {
        Method found = null;

        for (var c : dex.getClasses()) {
            if (c.getType().equals(OWNER)) {
                for (var m : c.getDirectMethods()) {
                    if (m.getName().equals("<init>")) {
                        require(
                                found == null
                                        && m.getAccessFlags() == 65536
                                        && m.getReturnType().equals("V")
                                        && m.getParameterTypes()
                                                .equals(
                                                        List.of(
                                                                "Landroid/content/Context;",
                                                                "Lcom/android/settingslib/bluetooth/LocalBluetoothAdapter;",
                                                                "Lcom/android/settingslib/bluetooth/CachedBluetoothDeviceManager;",
                                                                "Lcom/android/settingslib/bluetooth/BluetoothEventManager;")));
                        found = m;
                    }
                }
            }
        }

        require(found != null && found.getImplementation() != null);

        return found;
    }

    static void validateMethod(Method m) {
        var b = m.getImplementation();

        require(b.getRegisterCount() == 6 && b.getTryBlocks().isEmpty());
        int at = 0, checked = 0, queries = 0;

        for (var i : b.getInstructions()) {
            if (i instanceof ReferenceInstruction r
                    && r.getReference().toString().contains("->featureRoleSwitchSupported()Z")) {
                queries++;
            }

            if (at == 37) {
                require(
                        i.getOpcode() == Opcode.INVOKE_VIRTUAL
                                && i instanceof FiveRegisterInstruction fiveRegisterInstruction
                                && fiveRegisterInstruction.getRegisterC() == 2
                                && fiveRegisterInstruction.getRegisterCount() == 1
                                && ((ReferenceInstruction) i)
                                        .getReference()
                                        .toString()
                                        .equals(
                                                "Landroid/bluetooth/BluetoothAdapter;->featureRoleSwitchSupported()Z"));
                checked++;
            }

            if (at == 40) {
                require(
                        i.getOpcode() == Opcode.MOVE_RESULT
                                && ((OneRegisterInstruction) i).getRegisterA() == 3);
                checked++;
            }

            if (at == 41) {
                require(
                        i.getOpcode() == Opcode.IF_EQZ
                                && ((OneRegisterInstruction) i).getRegisterA() == 3
                                && ((OffsetInstruction) i).getCodeOffset() == 7);
                checked++;
            }

            if (at == 45) {
                require(
                        i.getOpcode() == Opcode.INVOKE_VIRTUAL
                                && ((ReferenceInstruction) i)
                                        .getReference()
                                        .toString()
                                        .equals(
                                                "Landroid/bluetooth/BluetoothAdapter;->addRoleSwitchListener(Landroid/bluetooth/BluetoothAdapter$RoleSwitchListener;)V"));
                checked++;
            }

            at += i.getCodeUnits();
        }

        require(at == 56 && checked == 4 && queries == 1);
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        String pin = FilesSupport.hash(original);

        require(api == 30 && PINS.contains(pin) && dex != null);
        validateMethod(method(new DexBackedDexFile(Opcodes.forApi(30), dex)));
    }

    static ImmutableDexFile rewrite(DexFile source, Method original, boolean inverse) {
        var types = new ArrayList<ClassDef>();
        var originals = new HashMap<Integer, Instruction>();
        int p = 0;

        for (var i : original.getImplementation().getInstructions()) {
            originals.put(p, i);
            p += i.getCodeUnits();
        }

        for (var c : source.getClasses()) {
            if (!c.getType().equals(OWNER)) {
                types.add(c);
                continue;
            }

            var methods = new ArrayList<Method>();

            for (var m : c.getDirectMethods()) {
                if (m.getName().equals("<init>")) {
                    var b = m.getImplementation();
                    var list = new ArrayList<Instruction>();
                    int at = 0;

                    for (var i : b.getInstructions()) {
                        if (at == 37 || at == 40) {
                            if (inverse) {
                                require(
                                        at == 37
                                                ? i.getOpcode() == Opcode.CONST
                                                        && ((OneRegisterInstruction) i)
                                                                        .getRegisterA()
                                                                == 3
                                                        && ((WideLiteralInstruction) i)
                                                                        .getWideLiteral()
                                                                == 0
                                                : i.getOpcode() == Opcode.NOP);
                                list.add(originals.get(at));
                            } else {
                                list.add(
                                        at == 37
                                                ? new ImmutableInstruction31i(Opcode.CONST, 3, 0)
                                                : new ImmutableInstruction10x(Opcode.NOP));
                            }
                        } else {
                            list.add(i);
                        }

                        at += i.getCodeUnits();
                    }

                    m =
                            new ImmutableMethod(
                                    m.getDefiningClass(),
                                    m.getName(),
                                    m.getParameters(),
                                    m.getReturnType(),
                                    m.getAccessFlags(),
                                    m.getAnnotations(),
                                    m.getHiddenApiRestrictions(),
                                    new ImmutableMethodImplementation(
                                            b.getRegisterCount(),
                                            list,
                                            b.getTryBlocks(),
                                            b.getDebugItems()));
                }

                methods.add(m);
            }

            types.add(
                    new ImmutableClassDef(
                            c.getType(),
                            c.getAccessFlags(),
                            c.getSuperclass(),
                            c.getInterfaces(),
                            c.getSourceFile(),
                            c.getAnnotations(),
                            c.getStaticFields(),
                            c.getInstanceFields(),
                            methods,
                            c.getVirtualMethods()));
        }

        return new ImmutableDexFile(Opcodes.forApi(30), types);
    }

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        validate(profile, api, original, dex);
        var before = new DexBackedDexFile(Opcodes.forApi(30), dex);
        var m = method(before);
        byte[] output = FrameworkDexMaterializer.serialize(rewrite(before, m, false));
        var normalized = rewrite(new DexBackedDexFile(Opcodes.forApi(30), output), m, true);
        var byName = new HashMap<String, ClassDef>();

        for (var c : normalized.getClasses()) {
            byName.put(c.getType(), c);
        }

        require(byName.size() == before.getClasses().size());

        for (var c : before.getClasses()) {
            require(
                    FrameworkDexMaterializer.semantic(c)
                            .equals(FrameworkDexMaterializer.semantic(byName.get(c.getType()))));
        }

        return output;
    }
}
