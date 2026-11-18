package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21t;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableStringReference;

import java.util.ArrayList;
import java.util.List;

/** Preserve Bluetooth startup when the desktop has no accessible automotive power service. */
final class SystemPolicyPowerGuard {
    static final String OWNER = "Lcom/qinggan/systempolicy/bluetooth/BluetoothSource;";
    private static final String CAR = "Landroid/car/Car;";
    private static final String POWER = "Landroid/car/hardware/power/CarPowerManager;";
    private static final String LIFECYCLE = OWNER.substring(0, OWNER.length() - 1) + "$16;";
    private static final String LISTENER =
            OWNER.substring(0, OWNER.length() - 1) + "$PowerStateListener;";

    private static final List<String> ORIGINAL =
            List.of(
                    "iget-object v0, p0, " + OWNER + "->mContext:Landroid/content/Context;",
                    "new-instance v1, " + LIFECYCLE,
                    "invoke-direct {v1, p0}, " + LIFECYCLE + "-><init>(" + OWNER + ")V",
                    "const/4 v2, 0x0",
                    "const-wide/16 v3, 0x0",
                    "invoke-static {v0, v2, v3, v4, v1}, "
                            + CAR
                            + "->createCar(Landroid/content/Context;Landroid/os/Handler;JLandroid/car/Car$CarServiceLifecycleListener;)Landroid/car/Car;",
                    "move-result-object v0",
                    "iput-object v0, p0, " + OWNER + "->mCar:" + CAR,
                    "iget-object v0, p0, " + OWNER + "->mCar:" + CAR,
                    "const-string v1, \"power\"",
                    "invoke-virtual {v0, v1}, "
                            + CAR
                            + "->getCarManager(Ljava/lang/String;)Ljava/lang/Object;",
                    "move-result-object v0",
                    "check-cast v0, " + POWER,
                    "iput-object v0, p0, " + OWNER + "->mCarPowerManager:" + POWER,
                    "iget-object v0, p0, " + OWNER + "->mCarPowerManager:" + POWER,
                    "iget-object v1, p0, " + OWNER + "->mCarPowerStateListener:" + LISTENER,
                    "invoke-virtual {v0, v1}, "
                            + POWER
                            + "->setListener(Landroid/car/hardware/power/CarPowerManager$CarPowerStateListener;)V",
                    "return-void");

    private static final List<String> CHARGING_ORIGINAL =
            List.of(
                    "new-instance v0, Landroid/content/Intent;",
                    "invoke-direct {v0}, Landroid/content/Intent;-><init>()V",
                    "invoke-virtual {p0}, "
                            + SystemPolicyHardwareGuard.SERVICE
                            + "->getPackageName()Ljava/lang/String;",
                    "move-result-object v1",
                    "const-string v2,"
                        + " \"com.qinggan.systempolicy.bluetooth.chargestation.service.ChargingService\"",
                    "invoke-virtual {v0, v1, v2},"
                        + " Landroid/content/Intent;->setClassName(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;",
                    "invoke-virtual {p0, v0}, "
                            + SystemPolicyHardwareGuard.SERVICE
                            + "->startService(Landroid/content/Intent;)Landroid/content/ComponentName;",
                    "return-void");

    private SystemPolicyPowerGuard() {}

    static void validateCharging(DexClasses classes) {
        require(classes.entry(SystemPolicyHardwareGuard.SERVICE) != null);
        Method selected = charging(classes);
        require(
                selected != null
                        && selected.getParameterTypes().isEmpty()
                        && selected.getReturnType().equals("V")
                        && selected.getAccessFlags() == 2
                        && selected.getImplementation() != null
                        && selected.getImplementation().getRegisterCount() == 4
                        && selected.getImplementation().getTryBlocks().isEmpty());
        require(
                DexEdits.render(selected.getImplementation(), new DexText.Frame(4, 1))
                        .equals(CHARGING_ORIGINAL));
    }

    private static Method charging(DexClasses classes) {
        Method selected = null;

        for (Method candidate : classes.get(SystemPolicyHardwareGuard.SERVICE).getMethods()) {
            if (candidate.getName().equals("startChargingService")) {
                require(selected == null);
                selected = candidate;
            }
        }

        return selected;
    }

    private static void require(boolean value) {
        Failure.require(
                value,
                "SYSTEMPOLICY_POWER_CONTRACT",
                "Exact API30 SystemPolicy automotive power gateway required",
                5);
    }

    /** Validate the whole original gateway before any SystemPolicy method is changed. */
    static Method validate(DexClasses classes) {
        require(classes.entry(OWNER) != null);
        Method selected = null;

        for (Method candidate : classes.get(OWNER).getVirtualMethods()) {
            if (candidate.getName().equals("initPowerMgr")) {
                require(selected == null);
                selected = candidate;
            }
        }

        require(
                selected != null
                        && selected.getParameterTypes().isEmpty()
                        && selected.getReturnType().equals("V")
                        && selected.getAccessFlags() == 0
                        && selected.getImplementation() != null
                        && selected.getImplementation().getRegisterCount() == 6
                        && selected.getImplementation().getTryBlocks().isEmpty());
        require(
                DexEdits.render(selected.getImplementation(), new DexText.Frame(6, 1))
                        .equals(ORIGINAL));

        return selected;
    }

    static void apply(DexClasses classes) {
        Method original = validate(classes);

        replace(classes, original, guarded(original, 0, 0x23));
        Method charging = charging(classes);
        // The prevalidated OEM body now begins with the existing four adapter instructions.
        replace(classes, charging, guarded(charging, 4, 0x18));
    }

    private static Method guarded(Method original, int index, int returnAddress) {
        var body = new MutableMethodImplementation(original.getImplementation());
        var unsupported = body.newLabelForAddress(returnAddress);

        body.addInstruction(
                index,
                new BuilderInstruction21c(
                        Opcode.CONST_STRING, 0, new ImmutableStringReference("car_service")));
        body.addInstruction(
                index + 1,
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
        body.addInstruction(index + 2, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0));
        body.addInstruction(index + 3, new BuilderInstruction21t(Opcode.IF_EQZ, 0, unsupported));

        if (index == 4) {
            // Keep the existing adapter-present branch entering the new service guard.
            body.replaceInstruction(
                    2, new BuilderInstruction21t(Opcode.IF_NEZ, 0, body.newLabelForIndex(index)));
        }

        return new ImmutableMethod(
                original.getDefiningClass(),
                original.getName(),
                original.getParameters(),
                original.getReturnType(),
                original.getAccessFlags(),
                original.getAnnotations(),
                original.getHiddenApiRestrictions(),
                body);
    }

    private static void replace(DexClasses classes, Method original, Method guarded) {
        var owner = classes.get(original.getDefiningClass());
        var direct = new ArrayList<Method>();

        for (Method candidate : owner.getDirectMethods()) {
            direct.add(
                    DexEdits.descriptor(candidate).equals(DexEdits.descriptor(original))
                            ? guarded
                            : candidate);
        }

        var virtual = new ArrayList<Method>();

        for (Method candidate : owner.getVirtualMethods()) {
            virtual.add(
                    DexEdits.descriptor(candidate).equals(DexEdits.descriptor(original))
                            ? guarded
                            : candidate);
        }

        classes.replace(
                new ImmutableClassDef(
                        owner.getType(),
                        owner.getAccessFlags(),
                        owner.getSuperclass(),
                        owner.getInterfaces(),
                        owner.getSourceFile(),
                        owner.getAnnotations(),
                        owner.getStaticFields(),
                        owner.getInstanceFields(),
                        direct,
                        virtual));
    }
}
