package com.voyemu.prepare;

import java.util.List;
import java.util.Map;

/**
 * The OEM SystemPolicy of the Free profiles skips its Bluetooth policy and charging service when
 * the emulator has no Bluetooth adapter. API30 also skips the automotive power gateway and charging
 * service when the desktop has no accessible CarService; the genuine service-present path remains
 * unchanged.
 */
final class SystemPolicyHardwareGuard {
    /** The API30 recipe; the API28 recipe of Free A9 is {@link #API28_RECIPE}. */
    static final String NAME = "api30-systempolicy-hardware";

    static final String API28_RECIPE = "systempolicy-hardware.json";

    /**
     * Shared libraries of the API30 SystemPolicy: the OEM image has QGAPI and android.car on its
     * BOOTCLASSPATH, the guest gets them as libraries of the same OEM jars.
     */
    static final List<String> API30_LIBRARIES = List.of("QGAPI", "android.car");

    static final String MANAGER = "Lcom/qinggan/systempolicy/PolicyManager;";
    static final String SERVICE = "Lcom/qinggan/systempolicy/SystemPolicyService;";
    private static final String ADAPTER =
            "invoke-static {}, Landroid/bluetooth/BluetoothAdapter;->getDefaultAdapter()"
                    + "Landroid/bluetooth/BluetoothAdapter;";

    /** Pin of the SystemPolicy-release original of each Free profile. */
    static final Map<String, String> PINS =
            Map.of(
                    "free-09-chinese-20260106-6.0.3",
                    "6c223315e64d14f9f1c715c42474257df772ce218e40094cf91b1cb012b2fadc",
                    "free-11-chinese-20250710-6.11.1",
                    "02749c34c515b8dd2ecdb6f5030762c292bc47fc3b79214d7965cb5cf56226eb",
                    "free-11-chinese-20260731-7.3.1",
                    "a81710fcbb382d9a1c449b240c2dc40aebc3d2e3cc309339ec1be3c5fbb2de00");

    /**
     * PolicyManager.<init> jumps over the BluetoothPolicy construction to the Build.PRODUCT check
     * that follows it; startChargingService returns before it builds the service intent.
     */
    static final List<DexEdits.MethodEdit> EDITS =
            List.of(
                    new DexEdits.MethodEdit(
                            MANAGER + "-><init>(Landroid/content/Context;)V",
                            null,
                            List.of(
                                    DexEdits.Edit.before(
                                            List.of(
                                                    "iget-object v0, p0, "
                                                            + MANAGER
                                                            + "->mPolicyList:Ljava/util/ArrayList;",
                                                    "new-instance v1,"
                                                        + " Lcom/qinggan/systempolicy/bluetooth/BluetoothPolicy;"),
                                            List.of(
                                                    ADAPTER,
                                                    "move-result-object v0",
                                                    "if-eqz v0, :voyemu_no_bluetooth_policy")),
                                    DexEdits.Edit.before(
                                            List.of(
                                                    "sget-object v0,"
                                                        + " Landroid/os/Build;->PRODUCT:Ljava/lang/String;"),
                                            List.of(":voyemu_no_bluetooth_policy")))),
                    new DexEdits.MethodEdit(
                            SERVICE + "->startChargingService()V",
                            null,
                            List.of(
                                    DexEdits.Edit.before(
                                            List.of("new-instance v0, Landroid/content/Intent;"),
                                            List.of(
                                                    ADAPTER,
                                                    "move-result-object v0",
                                                    "if-nez v0, :voyemu_has_bluetooth_adapter",
                                                    "return-void",
                                                    ":voyemu_has_bluetooth_adapter")))));

    private SystemPolicyHardwareGuard() {}

    static List<String> pins(String profile) {
        String pin = PINS.get(profile);

        return pin == null ? List.of() : List.of(pin);
    }

    /** Applies both guards to the classes of the exact SystemPolicy original of the profile. */
    static void apply(String profile, int api, String original, DexClasses classes) {
        Failure.require(
                PINS.containsKey(profile)
                        && PINS.get(profile).equals(original)
                        && api == (profile.startsWith("free-09-") ? 28 : 30),
                "SYSTEMPOLICY_CONTRACT",
                "Exact Free SystemPolicy APK/profile/API required",
                5);

        if (api == 30) {
            SystemPolicyPowerGuard.validate(classes);
            SystemPolicyPowerGuard.validateCharging(classes);
        }

        classes.edit(EDITS);

        if (api == 30) {
            SystemPolicyPowerGuard.apply(classes);
        }
    }
}
