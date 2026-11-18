package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Field;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.value.StringEncodedValue;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

/** Semantic original-consumer contract; acquisition integrity belongs to the profile input pins. */
final class PhoneConsumerContract {
    static final String BT = "Lcom/qinggan/bluetooth/", PHONE = "Lcom/qinggan/bluetoothphone/";

    static void need(boolean value, String reason) {
        Failure.require(value, "PHONE_CONSUMER_CONTRACT", reason, 5);
    }

    static Map<String, ClassDef> read(Path apk) throws Exception {
        var result = new TreeMap<String, ClassDef>();
        var container = DexFileFactory.loadDexContainer(apk.toFile(), Opcodes.forApi(30));

        for (String entry : container.getDexEntryNames()) {
            for (ClassDef type : container.getEntry(entry).getDexFile().getClasses()) {
                need(result.put(type.getType(), type) == null, "Duplicate original class");
            }
        }

        return result;
    }

    static ClassDef type(Map<String, ClassDef> classes, String name) {
        ClassDef result = classes.get(name);

        need(result != null, "Missing executable owner " + name);

        return result;
    }

    static Field field(ClassDef owner, String name, String descriptor, int flags) {
        Field found = null;

        for (Field value : owner.getFields()) {
            if (value.getName().equals(name)) {
                need(found == null, "Duplicate owner field");
                found = value;
            }
        }

        need(
                found != null
                        && found.getType().equals(descriptor)
                        && found.getAccessFlags() == flags,
                "Changed owner field " + name);

        return found;
    }

    static Method method(ClassDef owner, String signature, int flags) {
        Method found = null;

        for (Method value : owner.getMethods()) {
            if ((value.getName()
                            + "("
                            + String.join("", value.getParameterTypes())
                            + ")"
                            + value.getReturnType())
                    .equals(signature)) {
                need(found == null, "Duplicate owner method");
                found = value;
            }
        }

        need(
                found != null
                        && found.getAccessFlags() == flags
                        && found.getImplementation() != null,
                "Changed executable owner method " + signature);

        return found;
    }

    static void verify(Map<String, ClassDef> classes) {
        need(
                classes.keySet().stream()
                        .noneMatch(n ->
                                n.startsWith("Lcom/voyemu/input/PhoneControlProvider")
                                        || n.startsWith("Lcom/voyemu/input/PhonePeerProvider")),
                "Original already contains provider classes");

        for (String name :
                List.of(
                        "AppMain",
                        "activity/MainActivity",
                        "logic/component/ContactsProvider",
                        "logic/manager/HeadSetProfileManager",
                        "logic/manager/PbapProfileManager")) {
            type(classes, PHONE + name + ";");
        }

        ClassDef manager = type(classes, BT + "BTManager;");
        need(manager.getAccessFlags() == 0x11, "BTManager must be public final");
        field(manager, "mAdapter", BT + "profile/BTAdapter;", 2);
        method(manager, "getInstance(Landroid/content/Context;)" + BT + "BTManager;", 0x20009);
        method(
                manager,
                "getAdapter(" + BT + "profile/IBTCallback;)" + BT + "profile/BTAdapter;",
                1);
        ClassDef factory = type(classes, BT + "BTAdapterFactory;");
        Field vendor = field(factory, "sVender", "Ljava/lang/String;", 10),
                path = field(factory, "JAR_PATH", "Ljava/lang/String;", 26);
        need(
                vendor.getInitialValue() instanceof StringEncodedValue v
                        && v.getValue().equals("Ivt8"),
                "Factory vendor must select Ivt8");
        need(
                path.getInitialValue() instanceof StringEncodedValue v
                        && v.getValue().equals("/system/framework/"),
                "Factory system framework path changed");
        Method loader =
                method(
                        factory,
                        "getBTAdapter(Landroid/content/Context;"
                                + BT
                                + "BTDeviceManager;)"
                                + BT
                                + "profile/BTAdapter;",
                        8);
        Set<String> refs = new TreeSet<>();

        for (var instruction : loader.getImplementation().getInstructions()) {
            if (instruction instanceof ReferenceInstruction ref) {
                refs.add(ref.getReference().toString());
            }
        }

        need(
                refs.containsAll(
                        Set.of(
                                "Ldalvik/system/PathClassLoader;-><init>(Ljava/lang/String;Ljava/lang/ClassLoader;)V",
                                "Ljava/lang/Class;->getClassLoader()Ljava/lang/ClassLoader;",
                                "Ljava/lang/Class;->forName(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;",
                                "Ljava/lang/Class;->getDeclaredMethod(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;",
                                "Landroid/content/Context;",
                                BT + "BTDeviceManager;",
                                "getInstance",
                                "com.qinggan.bluetooth.internal.",
                                "BluetoothAdapter.jar")),
                "Factory reflection/classloader contract changed");
        String cacheName = PHONE + "logic/manager/CacheCallDataManager;";
        ClassDef cache = type(classes, cacheName);
        field(cache, "isCallLogInserting", "Z", 2);
        method(cache, "getInstance()" + cacheName, 0x20009);
        ClassDef callback =
                type(classes, PHONE + "logic/manager/HeadSetProfileManager$HeadSetProfile$2;");
        need(
                (BT + "profile/SimpleHfpCallback;").equals(callback.getSuperclass())
                        && type(classes, BT + "profile/SimpleHfpCallback;")
                                .getInterfaces()
                                .contains(BT + "profile/IHfpCallback;"),
                "OEM HFP callback identity changed");
    }
}
