package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Field;
import org.jf.dexlib2.iface.Method;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

/** Free A11 phone-owned adapter; owner declarations are compile dependencies only. */
final class BluetoothPeerComponent {
    static final String PREFIX = "Lcom/qinggan/bluetooth/";
    static final String SHARED = "voyemu/profiles/common/components/bluetooth/src/";

    static void check(boolean value, String message) {
        Failure.require(value, "QINGGAN11_PHONE_PEER_ABI", message, 3);
    }

    static String descriptor(Method method) {
        return method.getName()
                + "("
                + String.join("", method.getParameterTypes())
                + ")"
                + method.getReturnType();
    }

    static void field(Map<String, ClassDef> classes, String type, String name, String descriptor) {
        ClassDef owner = classes.get(PREFIX + type + ";");

        check(owner != null, "missing " + type);
        boolean found = false;

        for (Field value : owner.getFields()) {
            if (value.getName().equals(name)) {
                found =
                        value.getType().equals(descriptor)
                                && (value.getAccessFlags() & 8) == 0
                                && (type.equals("profile/SimpleHfpCallback")
                                        || type.equals("profile/SimplePbapCallback")
                                        || (value.getAccessFlags() & 5) != 0);
            }
        }

        check(found, "missing/incompatible instance field " + type + "." + name + ":" + descriptor);
    }

    static void method(Map<String, ClassDef> classes, String type, String signature) {
        ClassDef owner = classes.get(PREFIX + type + ";");

        check(owner != null, "missing " + type);
        boolean found = false;

        for (Method value : owner.getMethods()) {
            if (descriptor(value).equals(signature)) {
                found = (value.getAccessFlags() & 8) == 0 && (value.getAccessFlags() & 5) != 0;
            }
        }

        check(found, "missing/incompatible instance method " + type + "." + signature);
    }

    static void constant(Map<String, ClassDef> classes, String name, int expected) {
        ClassDef type = classes.get(PREFIX + "BTConst;");

        check(type != null, "missing BTConst");
        boolean found = false;

        for (Field field : type.getFields()) {
            if (field.getName().equals(name)) {
                found =
                        field.getType().equals("I")
                                && (field.getAccessFlags() & 24) == 24
                                && field.getInitialValue()
                                        instanceof org.jf.dexlib2.iface.value.IntEncodedValue value
                                && value.getValue() == expected;
            }
        }

        check(found, "incompatible constant " + name);
    }

    static void contract(Map<String, ClassDef> classes) {
        String[] names = {
            "CALL_STATE_ACTIVE",
            "CALL_STATE_DIALING",
            "CALL_STATE_INCOMING",
            "CALL_STATE_TERMINATED",
            "CONNECTION_STATE_CONNECTED",
            "CONNECTION_STATE_DISCONNECTED",
            "PBAP_RESPONSE_TYPE_CALLBACK",
            "PBAP_SYNC_STATE_IDLE",
            "PBAP_SYNC_STATE_STARTED",
            "PBAP_SYNC_STATE_FINISHED",
            "PBAP_SYNC_STATE_ABORTED",
            "PHONE_NUMBER_TYPE_CELL"
        };
        int[] values = {128, 129, 130, 131, 16, 19, 176, 95, 96, 97, 98, 160};

        for (int i = 0; i < names.length; i++) {
            constant(classes, names[i], values[i]);
        }

        field(classes, "profile/BTAdapter", "mHandler", "Landroid/os/Handler;");
        field(classes, "profile/BTAdapter", "mHfp", PREFIX + "profile/Hfp;");
        field(classes, "profile/BTAdapter", "mPbap", PREFIX + "profile/Pbap;");
        field(classes, "profile/BTAdapter", "mA2dp", PREFIX + "profile/A2dp;");
        field(classes, "profile/BTAdapter", "mIsReady", "Z");
        field(classes, "profile/SimpleHfpCallback", "currentDevice", PREFIX + "profile/BTDevice;");
        field(
                classes,
                "profile/SimplePbapCallback",
                "currentSyncDevice",
                PREFIX + "profile/BTDevice;");
        field(classes, "bean/BTContactItem", "numbers", "Ljava/util/List;");
        method(
                classes,
                "profile/BTAdapter",
                "<init>(Landroid/content/Context;" + PREFIX + "BTDeviceManager;)V");
        method(
                classes,
                "profile/Hfp",
                "<init>(Landroid/content/Context;" + PREFIX + "BTDeviceManager;)V");
        method(
                classes,
                "profile/Pbap",
                "<init>(Landroid/content/Context;" + PREFIX + "BTDeviceManager;)V");
        method(
                classes,
                "profile/A2dp",
                "<init>(Landroid/content/Context;" + PREFIX + "BTDeviceManager;)V");
        method(classes, "profile/BTDevice", "<init>(Landroid/content/Context;)V");
        method(classes, "profile/IHfpCallback", "on3WayCallStateChanged(IILjava/lang/String;)V");
        method(
                classes,
                "profile/IPbapCallback",
                "onContactItemFetched(" + PREFIX + "bean/BTContactItem;)V");
        method(
                classes,
                "profile/IBTProfileCallback",
                "onStateChanged(" + PREFIX + "profile/BTDevice;II)V");
        method(classes, "profile/IBTProfileCallback", "onReady()V");
        method(classes, "bean/BTContactItem", "<init>(Ljava/lang/String;)V");
        method(classes, "bean/BTPhoneNumber", "<init>(ILjava/lang/String;)V");
        method(
                classes,
                "profile/BluetoothCall",
                "<init>(" + PREFIX + "profile/BTDevice;IZLjava/lang/String;ZII)V");
    }

    static Map<String, ClassDef> read(Path donor) throws Exception {
        var out = new TreeMap<String, ClassDef>();
        var container = DexFileFactory.loadDexContainer(donor.toFile(), Opcodes.forApi(30));

        for (String name : container.getDexEntryNames()) {
            for (ClassDef type : container.getEntry(name).getDexFile().getClasses()) {
                if (type.getType().startsWith(PREFIX)
                        && !type.getType().startsWith(PREFIX + "internal/")) {
                    check(
                            out.put(type.getType(), type) == null,
                            "duplicate owner " + type.getType());
                }
            }
        }

        check(!out.isEmpty(), "no owner Bluetooth classes");

        return out;
    }

    // Each peer source is the shared one or one of a component of the selected profile itself, as
    // Free 6.11.1 declares its own adapter and subscription.
    private static void checkSources(
            ProfileRegistry.Profile profile, Map<String, Object> files, Set<String> own) {
        for (String file : own) {
            String value = String.valueOf(files.get("src/" + file));
            check(
                    value.equals(SHARED + file)
                            || value.matches(
                                    java.util.regex.Pattern.quote(
                                                    "voyemu/profiles/"
                                                            + profile.id()
                                                            + "/components/")
                                            + "[a-z0-9][a-z0-9-]*/src/"
                                            + java.util.regex.Pattern.quote(file)),
                    "shared or own peer source required");
        }
    }

    /** The pinned QGAPI and BluetoothPhone originals and the Android platform jar. */
    record CompileInputs(Path qgapi, Path phone, Path platform) {}

    static Path build(
            ProfileRegistry.Profile profile,
            int api,
            String name,
            Map<String, Object> definition,
            Path source,
            CompileInputs inputs,
            Path work)
            throws Exception {
        Path qgapi = inputs.qgapi(), phone = inputs.phone(), platform = inputs.platform();

        check(
                api == 30 && name.equals("bluetooth"),
                "registered Free A11 API30 Bluetooth component required");
        check(
                definition
                                .keySet()
                                .equals(
                                        Set.of(
                                                "type",
                                                "minSdk",
                                                "output",
                                                "sourceFiles",
                                                "qinggan11PhonePeer"))
                        && Boolean.TRUE.equals(definition.get("qinggan11PhonePeer"))
                        && Json.str(definition, "type").equals("jar")
                        && Json.integer(definition, "minSdk") == 30
                        && Json.str(definition, "output").equals("Ivt8BluetoothAdapter.jar"),
                "finite Free A11 peer recipe required");
        Set<String> own =
                Set.of(
                        "Ivt8BTAdapter.java",
                        "SyntheticPhonePeer.java",
                        "PeerConfiguration.java",
                        "PeerSubscription.java");
        var files = Json.obj(definition, "sourceFiles");
        check(
                files.keySet()
                        .equals(
                                own.stream()
                                        .map(n -> "src/" + n)
                                        .collect(java.util.stream.Collectors.toSet())),
                "four existing peer sources required");
        checkSources(profile, files, own);
        check(
                GuestJava.sources(source.resolve("src")).equals(own),
                "no extra sources besides the compile-only declarations allowed");
        var originals = Json.obj(profile.data(), "original");
        NativeCore.pinned(qgapi, Json.str(Json.obj(originals, "QGAPI"), "sha256"));
        NativeCore.pinned(phone, Json.str(Json.obj(originals, "BluetoothPhone"), "sha256"));
        var framework = read(qgapi);
        var consumer = read(phone);
        contract(framework);
        contract(consumer);
        Path dex =
                GuestJava.dex(
                        new GuestJava.Unit(source.resolve("src"), 30),
                        platform,
                        work.resolve("java"));
        var entries = new TreeMap<String, byte[]>();
        Set<String> classes = new TreeSet<>();

        for (var file : FilesSupport.tree(dex).entrySet()) {
            check(file.getKey().equals("classes.dex"), "single own peer DEX required");
            entries.put(file.getKey(), Files.readAllBytes(file.getValue()));

            for (ClassDef type :
                    DexFileFactory.loadDexFile(file.getValue().toFile(), Opcodes.forApi(30))
                            .getClasses()) {
                check(
                        type.getType().startsWith(PREFIX + "internal/ivt8/")
                                && classes.add(type.getType()),
                        "only distinct own peer classes allowed");
            }
        }

        check(
                classes.contains(PREFIX + "internal/ivt8/Ivt8BTAdapter;")
                        && classes.contains(PREFIX + "internal/ivt8/SyntheticPhonePeer;"),
                "adapter and state machine required");
        Path output = work.resolve("Ivt8BluetoothAdapter.jar");
        NativeCore.zip(output, entries, false);
        Files.writeString(
                work.resolve("peer-component.json"),
                Json.write(
                                Map.of(
                                        "schema",
                                        1,
                                        "framework_owner_classes",
                                        framework.size(),
                                        "phone_owner_classes",
                                        consumer.size(),
                                        "own_classes",
                                        classes.size(),
                                        "declarations_packaged",
                                        false,
                                        "phone_owns_compile_declarations",
                                        true,
                                        "runtime_verified",
                                        false))
                        + "\n");

        return output;
    }
}
