package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.OffsetInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction21t;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

/** Keep the optional automotive audio manager absent when the clean guest has no audio Binder. */
final class PassionVehicleAudioGuard {
    static final String PROFILE = "passion-11-russian-20260403-2.1.4.80",
            NAME = "passion-api30-vehicle-audio";
    static final String PIN = PassionVehicleBluetoothGuard.PIN;
    static final String OWNER = "Lcom/voyah/car/bluetooth/utils/AudioSwitchHelper;",
            METHOD = "bind$lambda-0";
    static final String OBSERVER = "Lcom/voyah/platform/provider/api/CarObserver;",
            SOUND = "Lcom/voyah/car/vm/common/sound/SoundValueViewModelN;";

    private static void require(boolean value) {
        Failure.require(
                value,
                "PASSION_VEHICLE_AUDIO_CONTRACT",
                "Exact owner-pinned Passion nullable automotive audio callback required",
                3);
    }

    static Method method(DexFile dex) {
        Method found = null;

        for (var c : dex.getClasses()) {
            if (c.getType().equals(OWNER)) {
                for (var m : c.getDirectMethods()) {
                    if (m.getName().equals(METHOD)) {
                        require(found == null);
                        found = m;
                    }
                }
            }
        }

        require(found != null);

        return found;
    }

    static void validateMethod(Method m) {
        require(
                m.getAccessFlags() == 26
                        && m.getParameterTypes().equals(List.of("Landroid/car/Car;", "Z"))
                        && m.getReturnType().equals("V"));
        var b = m.getImplementation();
        require(b != null && b.getRegisterCount() == 3 && b.getTryBlocks().isEmpty());
        int at = 0, checked = 0;

        for (var i : b.getInstructions()) {
            if (requireBindingInstruction(i, at)) {
                checked++;
            }

            at += i.getCodeUnits();
        }

        require(at == 62 && checked == 6);
    }

    private static boolean requireBindingInstruction(Instruction i, int at) {
        switch (at) {
            case 18 ->
                    require(
                            i.getOpcode() == Opcode.INVOKE_VIRTUAL
                                    && ((ReferenceInstruction) i)
                                            .getReference()
                                            .toString()
                                            .equals(
                                                    "Landroid/car/Car;->getCarManager(Ljava/lang/String;)Ljava/lang/Object;")
                                    && ((FiveRegisterInstruction) i).getRegisterCount() == 2
                                    && ((FiveRegisterInstruction) i).getRegisterC() == 1
                                    && ((FiveRegisterInstruction) i).getRegisterD() == 2);
            case 21 ->
                    require(
                            i.getOpcode() == Opcode.MOVE_RESULT_OBJECT
                                    && ((OneRegisterInstruction) i).getRegisterA() == 1);
            case 24 ->
                    require(
                            i.getOpcode() == Opcode.INVOKE_STATIC
                                    && ((ReferenceInstruction) i)
                                            .getReference()
                                            .toString()
                                            .equals(
                                                    "Lkotlin/jvm/internal/Intrinsics;->checkNotNull(Ljava/lang/Object;Ljava/lang/String;)V")
                                    && ((FiveRegisterInstruction) i).getRegisterCount() == 2
                                    && ((FiveRegisterInstruction) i).getRegisterC() == 1
                                    && ((FiveRegisterInstruction) i).getRegisterD() == 2);
            case 27 ->
                    require(
                            i.getOpcode() == Opcode.CHECK_CAST
                                    && ((OneRegisterInstruction) i).getRegisterA() == 1
                                    && ((ReferenceInstruction) i)
                                            .getReference()
                                            .toString()
                                            .equals("Landroid/car/media/CarAudioManager;"));
            case 29 ->
                    require(
                            i.getOpcode() == Opcode.SPUT_OBJECT
                                    && ((OneRegisterInstruction) i).getRegisterA() == 1
                                    && ((ReferenceInstruction) i)
                                            .getReference()
                                            .toString()
                                            .equals(
                                                    OWNER
                                                            + "->mCarAudioManager:Landroid/car/media/CarAudioManager;"));
            case 61 -> require(i.getOpcode() == Opcode.RETURN_VOID);
            default -> {
                return false;
            }
        }

        return true;
    }

    static Method nullableMethod(DexFile dex, String owner) {
        String name = owner.equals(OBSERVER) ? "loadCar" : "setVoiceProgress";
        Method found = null;

        for (var c : dex.getClasses()) {
            if (c.getType().equals(owner)) {
                for (var m : c.getVirtualMethods()) {
                    if (m.getName().equals(name)) {
                        require(found == null);
                        found = m;
                    }
                }
            }
        }

        require(found != null);

        return found;
    }

    static void validateNullable(Method m, boolean observer) {
        require(
                m.getAccessFlags() == 1
                        && m.getReturnType().equals("V")
                        && m.getParameterTypes()
                                .equals(
                                        observer
                                                ? List.of("Landroid/car/Car;")
                                                : List.of("I", "Z")));
        var b = m.getImplementation();
        require(
                b != null
                        && b.getRegisterCount() == (observer ? 3 : 5)
                        && b.getTryBlocks().isEmpty());
        int at = 0, checked = 0;

        for (var i : b.getInstructions()) {
            if (observer ? requireObserverInstruction(i, at) : requireSoundInstruction(i, at)) {
                checked++;
            }

            at += i.getCodeUnits();
        }

        require(at == (observer ? 29 : 80) && checked == (observer ? 5 : 4));
    }

    private static boolean requireObserverInstruction(Instruction i, int at) {
        switch (at) {
            case 11 ->
                    require(
                            i.getOpcode() == Opcode.INVOKE_VIRTUAL
                                    && ((ReferenceInstruction) i)
                                            .getReference()
                                            .toString()
                                            .equals(
                                                    "Landroid/car/Car;->getCarManager(Ljava/lang/String;)Ljava/lang/Object;"));
            case 14 ->
                    require(
                            i.getOpcode() == Opcode.MOVE_RESULT_OBJECT
                                    && ((OneRegisterInstruction) i).getRegisterA() == 2);
            case 15 ->
                    require(
                            i.getOpcode() == Opcode.IPUT_OBJECT
                                    && ((TwoRegisterInstruction) i).getRegisterA() == 2
                                    && ((ReferenceInstruction) i)
                                            .getReference()
                                            .toString()
                                            .equals(OBSERVER + "->mManager:Ljava/lang/Object;"));
            case 17 ->
                    require(
                            i.getOpcode() == Opcode.INVOKE_VIRTUAL
                                    && ((ReferenceInstruction) i)
                                            .getReference()
                                            .toString()
                                            .equals(OBSERVER + "->registerListener()V"));
            case 28 -> require(i.getOpcode() == Opcode.RETURN_VOID);
            default -> {
                return false;
            }
        }

        return true;
    }

    private static boolean requireSoundInstruction(Instruction i, int at) {
        switch (at) {
            case 52 ->
                    require(
                            i.getOpcode() == Opcode.IGET_OBJECT
                                    && ((TwoRegisterInstruction) i).getRegisterA() == 0
                                    && ((ReferenceInstruction) i)
                                            .getReference()
                                            .toString()
                                            .equals(
                                                    SOUND
                                                            + "->mCarAudioManager:Landroid/car/media/CarAudioManager;"));
            case 54 ->
                    require(
                            i.getOpcode() == Opcode.INVOKE_VIRTUAL
                                    && ((ReferenceInstruction) i)
                                            .getReference()
                                            .toString()
                                            .equals(
                                                    "Landroid/car/media/CarAudioManager;->setOneKeyMute(Z)I")
                                    && ((FiveRegisterInstruction) i).getRegisterC() == 0);
            case 57 ->
                    require(
                            i.getOpcode() == Opcode.IGET_OBJECT
                                    && ((ReferenceInstruction) i)
                                            .getReference()
                                            .toString()
                                            .equals(
                                                    SOUND
                                                            + "->mMegaCarAudioManager:Lcom/mega/nexus/media/MegaCarAudioManager;"));
            case 79 -> require(i.getOpcode() == Opcode.RETURN_VOID);
            default -> {
                return false;
            }
        }

        return true;
    }

    static Method rewriteNullable(Method m, boolean observer, boolean inverse) {
        var body = new MutableMethodImplementation(m.getImplementation());
        int guard = observer ? 17 : 54,
                target = observer ? 28 : 57,
                reg = observer ? 2 : 0,
                address = 0,
                index = -1;

        for (int i = 0; i < body.getInstructions().size(); i++) {
            if (address == guard) {
                index = i;
            }

            address += body.getInstructions().get(i).getCodeUnits();
        }

        require(index >= 0 && address == (observer ? 29 : 80) + (inverse ? 2 : 0));

        if (inverse) {
            var branch = body.getInstructions().get(index);
            require(
                    branch.getOpcode() == Opcode.IF_EQZ
                            && ((OneRegisterInstruction) branch).getRegisterA() == reg
                            && ((OffsetInstruction) branch).getCodeOffset() == target + 2 - guard);
            body.removeInstruction(index);
        } else {
            var end = body.newLabelForAddress(target);
            body.addInstruction(index, new BuilderInstruction21t(Opcode.IF_EQZ, reg, end));
        }

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                body);
    }

    static void validate(String profile, int api, Path original, byte[] dex) throws Exception {
        require(
                PROFILE.equals(profile)
                        && api == 30
                        && FilesSupport.hash(original).equals(PIN)
                        && dex != null);
        var source = new DexBackedDexFile(Opcodes.forApi(30), dex);
        validateMethod(method(source));
        validateNullable(nullableMethod(source, OBSERVER), true);
        validateNullable(nullableMethod(source, SOUND), false);
        PassionVehicleInstrumentGuard.validate(source);
    }

    static ImmutableDexFile rewrite(DexFile source, Method original, boolean inverse) {
        Instruction originalCheck = null;
        int address = 0;

        for (var i : original.getImplementation().getInstructions()) {
            if (address == 24) {
                originalCheck = i;
            }

            address += i.getCodeUnits();
        }

        require(originalCheck != null);
        var classes = new ArrayList<ClassDef>();

        for (var c : source.getClasses()) {
            if (c.getType().equals(OBSERVER) || c.getType().equals(SOUND)) {
                classes.add(routedNullableClass(c, inverse));
                continue;
            }

            if (!c.getType().equals(OWNER)) {
                classes.add(c);
                continue;
            }

            var direct = new ArrayList<Method>();

            for (var m : c.getDirectMethods()) {
                if (m.getName().equals(METHOD)) {
                    m = routedBinding(m, originalCheck, inverse);
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

        return PassionVehicleInstrumentGuard.rewrite(
                new ImmutableDexFile(Opcodes.forApi(30), classes), inverse);
    }

    private static ClassDef routedNullableClass(ClassDef c, boolean inverse) {
        boolean observer = c.getType().equals(OBSERVER);
        var virtual = new ArrayList<Method>();
        int changed = 0;

        for (var m : c.getVirtualMethods()) {
            if (m.getName().equals(observer ? "loadCar" : "setVoiceProgress")) {
                m = rewriteNullable(m, observer, inverse);
                changed++;
            }

            virtual.add(m);
        }

        require(changed == 1);

        return new ImmutableClassDef(
                c.getType(),
                c.getAccessFlags(),
                c.getSuperclass(),
                c.getInterfaces(),
                c.getSourceFile(),
                c.getAnnotations(),
                c.getStaticFields(),
                c.getInstanceFields(),
                c.getDirectMethods(),
                virtual);
    }

    private static Method routedBinding(Method m, Instruction originalCheck, boolean inverse) {
        var b = m.getImplementation();
        var list = new ArrayList<Instruction>();
        int at = 0, changed = 0;

        for (var i : b.getInstructions()) {
            if (at == 24) {
                if (inverse) {
                    require(
                            i.getOpcode() == Opcode.IF_EQZ
                                    && ((OneRegisterInstruction) i).getRegisterA() == 1
                                    && ((OffsetInstruction) i).getCodeOffset() == 37);
                    list.add(originalCheck);
                } else {
                    require(i.getOpcode() == Opcode.INVOKE_STATIC);
                    list.add(new ImmutableInstruction21t(Opcode.IF_EQZ, 1, 37));
                    list.add(new ImmutableInstruction10x(Opcode.NOP));
                }

                changed++;
            } else if (inverse && at == 26) {
                require(i.getOpcode() == Opcode.NOP);
                changed++;
            } else {
                list.add(i);
            }

            at += i.getCodeUnits();
        }

        require(at == 62 && changed == (inverse ? 2 : 1));

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

    static byte[] apply(String profile, int api, Path original, byte[] dex) throws Exception {
        validate(profile, api, original, dex);
        var before = new DexBackedDexFile(Opcodes.forApi(30), dex);
        var originalMethod = method(before);
        byte[] result = FrameworkDexMaterializer.serialize(rewrite(before, originalMethod, false));
        var normalized =
                rewrite(new DexBackedDexFile(Opcodes.forApi(30), result), originalMethod, true);
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
