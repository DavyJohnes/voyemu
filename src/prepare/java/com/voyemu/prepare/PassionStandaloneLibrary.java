package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Field;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableField;
import org.jf.dexlib2.immutable.ImmutableMethod;

import java.util.ArrayList;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

// Standalone shared libraries are not bootclasspath DEX. Retain all executable/member
// semantics, omitting only boot hidden-API metadata rejected by the pinned ART verifier.
final class PassionStandaloneLibrary {
    static final String PIN = "ec2933b1c42f1932cd46117a475b84d83371c9b19130930f43ffd4c455ca6bd3";
    static final Map<String, Set<String>> CLASSES =
            Map.ofEntries(
                    Map.entry(
                            "voyemu-platform-callbacks.jar",
                            Set.of(
                                    "Landroid/bluetooth/BluetoothAdapter$RoleSwitchListener;",
                                    "Landroid/bluetooth/BluetoothPbapClientCallback;",
                                    "Landroid/bluetooth/IRoleSwitchListener;",
                                    "Landroid/bluetooth/IRoleSwitchListener$Default;",
                                    "Landroid/bluetooth/IRoleSwitchListener$Stub;",
                                    "Landroid/bluetooth/IRoleSwitchListener$Stub$Proxy;",
                                    "Landroid/hardware/fingerprint/FingerprintManager$FingerprintDetectionCallback;")),
                    Map.entry(
                            "voyemu-window.jar",
                            Set.of(
                                    "Landroid/view/IWindowAttributesCallback;",
                                    "Landroid/view/IWindowAttributesCallback$Default;",
                                    "Landroid/view/IWindowAttributesCallback$Stub;",
                                    "Landroid/view/IWindowAttributesCallback$Stub$Proxy;",
                                    "Landroid/view/WindowAttributes;",
                                    "Landroid/view/WindowAttributes$1;")),
                    Map.entry(
                            "voyemu-mega.jar",
                            Set.of(
                                    "Lcom/mega/nexus/app/MegaActivityManagerProxyAdapter$TaskStackListener;",
                                    "Lcom/mega/nexus/app/MegaActivityManagerProxyAdapter;",
                                    "Lcom/mega/nexus/app/MegaTaskStackListener;",
                                    "Lcom/mega/nexus/bluetooth/MegaBluetoothA2dpAdapter;",
                                    "Lcom/mega/nexus/bluetooth/MegaBluetoothAdapterWrapper;",
                                    "Lcom/mega/nexus/bluetooth/MegaBluetoothDeviceAdapter;",
                                    "Lcom/mega/nexus/bluetooth/MegaBluetoothHeadsetAdapter;",
                                    "Lcom/mega/nexus/bluetooth/MegaBluetoothPbapClientAdapter;",
                                    "Lcom/mega/nexus/bluetooth/MegaBluetoothProfileAdapter;",
                                    "Lcom/mega/nexus/bluetooth/MegaBluetoothUuidAdapter;",
                                    "Lcom/mega/nexus/content/res/MegaConfigurationAdapter;",
                                    "Lcom/mega/nexus/media/MegaAudioAttributesAdapter;",
                                    "Lcom/mega/nexus/media/MegaAudioManagerAdapter;",
                                    "Lcom/mega/nexus/net/MegaConnectivityManagerAdapter;",
                                    "Lcom/mega/nexus/net/wifi/MegaScanResultAdapter;",
                                    "Lcom/mega/nexus/net/wifi/MegaWifiConfigurationAdapter;",
                                    "Lcom/mega/nexus/net/wifi/MegaWifiInfoAdapter;",
                                    "Lcom/mega/nexus/net/wifi/MegaWifiManagerAdapter;",
                                    "Lcom/mega/nexus/os/MegaPowerManagerAdapter;",
                                    "Lcom/mega/nexus/os/MegaUserHandleAdapter;",
                                    "Lcom/mega/nexus/os/storage/MegaStorageVolumeAdapter;",
                                    "Lcom/mega/nexus/service/voice/MegaVoiceInteractionServiceAdapter;",
                                    "Lcom/mega/nexus/view/MegaKeyEventAdapter;",
                                    "Lcom/mega/nexus/view/MegaLayoutParamsAdapter;",
                                    "Lcom/mega/nexus/view/MegaSurfaceControlAdapter;",
                                    "Lcom/mega/nexus/view/MegaWindowAdapter;")));

    private static void require(boolean value) {
        Failure.require(
                value,
                "PASSION_LIBRARY_METADATA",
                "Exact Passion standalone donor-library metadata contract required",
                3);
    }

    static boolean selected(int api, String pin, String output, Set<String> classes) {
        if (!PIN.equals(pin)) {
            return false;
        }

        require(api == 30 && CLASSES.containsKey(output) && CLASSES.get(output).equals(classes));

        return true;
    }

    private static ImmutableField field(Field value) {
        return new ImmutableField(
                value.getDefiningClass(),
                value.getName(),
                value.getType(),
                value.getAccessFlags(),
                value.getInitialValue(),
                value.getAnnotations(),
                Set.of());
    }

    private static ImmutableMethod method(Method value) {
        return new ImmutableMethod(
                value.getDefiningClass(),
                value.getName(),
                value.getParameters(),
                value.getReturnType(),
                value.getAccessFlags(),
                value.getAnnotations(),
                Set.of(),
                value.getImplementation());
    }

    static ClassDef withoutBootMetadata(ClassDef value) {
        var statics = new ArrayList<Field>();

        value.getStaticFields().forEach(x -> statics.add(field(x)));
        var instances = new ArrayList<Field>();
        value.getInstanceFields().forEach(x -> instances.add(field(x)));
        var direct = new ArrayList<Method>();
        value.getDirectMethods().forEach(x -> direct.add(method(x)));
        var virtual = new ArrayList<Method>();
        value.getVirtualMethods().forEach(x -> virtual.add(method(x)));

        return new ImmutableClassDef(
                value.getType(),
                value.getAccessFlags(),
                value.getSuperclass(),
                value.getInterfaces(),
                value.getSourceFile(),
                value.getAnnotations(),
                statics,
                instances,
                direct,
                virtual);
    }

    static byte[] apply(byte[] input, Set<String> expected) throws Exception {
        var source = new DexBackedDexFile(Opcodes.forApi(30), input);
        var classes = new TreeMap<String, ClassDef>();

        for (ClassDef value : source.getClasses()) {
            require(classes.put(value.getType(), withoutBootMetadata(value)) == null);
        }

        require(classes.keySet().equals(expected));
        byte[] output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), classes.values()));
        var actual = new TreeMap<String, ClassDef>();

        for (ClassDef value : new DexBackedDexFile(Opcodes.forApi(30), output).getClasses()) {
            require(actual.put(value.getType(), value) == null);

            for (Field f : value.getFields()) {
                require(f.getHiddenApiRestrictions().isEmpty());
            }

            for (Method m : value.getMethods()) {
                require(m.getHiddenApiRestrictions().isEmpty());
            }
        }

        require(actual.keySet().equals(expected));

        for (var item : classes.entrySet()) {
            require(
                    FrameworkDexMaterializer.semantic(item.getValue())
                            .equals(FrameworkDexMaterializer.semantic(actual.get(item.getKey()))));
        }

        return output;
    }
}
