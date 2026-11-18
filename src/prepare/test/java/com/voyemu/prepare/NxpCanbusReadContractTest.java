package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.reference.FieldReference;
import org.jf.dexlib2.iface.reference.MethodReference;
import org.jf.dexlib2.iface.value.IntEncodedValue;

import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

import javax.tools.ToolProvider;

/**
 * Executes the provider's exact getter bodies; optional pinned DEX verifies the consumer and SDK
 * seam.
 */
public final class NxpCanbusReadContractTest {
    static final Set<String> WINDOWS =
            Set.of(
                    "fLWindow",
                    "fRWindow",
                    "rLWindow",
                    "rRWindow",
                    "fLWindowHotProtect",
                    "fRWindowHotProtect",
                    "rLWindowHotProtect",
                    "rRWindowHotProtect",
                    "fLWindowLearn",
                    "fRWindowLearn",
                    "rLWindowLearn",
                    "rRWindowLearn",
                    "fLWindowPosition",
                    "fRWindowPosition",
                    "rLWindowPosition",
                    "rRWindowPosition",
                    "roll",
                    "rollPosition",
                    "sunroof",
                    "sunroofPosition",
                    "sunroofTilt");
    static final Set<String> DOORS =
            Set.of(
                    "bonnetDoor",
                    "fLDoor",
                    "fRDoor",
                    "rLDoor",
                    "rRDoor",
                    "loadSpace",
                    "fLDoorLockStatus",
                    "fRDoorLockStatus",
                    "rLDoorLockStatus",
                    "rRDoorLockStatus");

    static void need(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    static String getter(String source, String name) {
        int end = source.indexOf(" " + name + "()"),
                start = source.lastIndexOf("public synchronized", end),
                open = source.indexOf('{', end),
                depth = 1,
                at = open + 1;

        need(end >= 0 && start >= 0 && open >= 0, "Actual provider getter missing: " + name);

        for (; depth > 0 && at < source.length(); at++) {
            if (source.charAt(at) == '{') {
                depth++;
            }

            if (source.charAt(at) == '}') {
                depth--;
            }
        }

        need(depth == 0, "Unbalanced getter");

        return source.substring(start, at);
    }

    static Path put(Path root, String name, String value) throws Exception {
        Path p = root.resolve(name);

        Files.createDirectories(p.getParent());
        Files.writeString(p, value);

        return p;
    }

    static String fields(Set<String> fields, String initial) {
        return fields.stream()
                .sorted()
                .map(name -> "public int " + name + initial + ";")
                .collect(java.util.stream.Collectors.joining());
    }

    static void behavior(Path root, Path work) throws Exception {
        Files.createDirectories(work);
        String source =
                Files.readString(
                        root.resolve(
                                "profiles/free-09-chinese-20221230-nxp-rc45/components/canbus/src/com/qinggan/canbus/service/ClimateService.java"));
        var files =
                List.of(
                        put(
                                work,
                                "com/qinggan/canbus/WindowStatus.java",
                                "package com.qinggan.canbus; public final class WindowStatus {"
                                        + " public static final int UNKNOWN=-1;"
                                        + fields(WINDOWS, "")
                                        + "}"),
                        put(
                                work,
                                "com/qinggan/canbus/DoorStatus.java",
                                "package com.qinggan.canbus; public final class DoorStatus {"
                                        + fields(DOORS, "=-1")
                                        + "}"),
                        put(
                                work,
                                "com/qinggan/canbus/GearState.java",
                                "package com.qinggan.canbus; public enum GearState"
                                        + " {Parking,Unknown}"),
                        put(
                                work,
                                "Provider.java",
                                "public final class Provider {"
                                        + getter(source, "getWindowStatus")
                                        + getter(source, "getDoorStatus")
                                        + getter(source, "getGearStatus")
                                        + "}"));
        var arguments =
                new java.util.ArrayList<String>(List.of("--release", "25", "-d", work.toString()));
        files.forEach(file -> arguments.add(file.toString()));
        need(
                ToolProvider.getSystemJavaCompiler()
                                .run(null, null, null, arguments.toArray(String[]::new))
                        == 0,
                "Exact provider getters must compile against the NXP SDK seam");

        try (var loader = new URLClassLoader(new java.net.URL[] {work.toUri().toURL()}, null)) {
            Class<?> type = loader.loadClass("Provider");
            Object provider = type.getConstructor().newInstance();
            Object window = type.getMethod("getWindowStatus").invoke(provider);
            // Same first dereference as VehicleHiboardBar.setupComponentState: old provider fails
            // here.
            need(
                    window != null,
                    "Launcher setupComponentState dereferences WindowStatus.fLWindow; provider"
                            + " returned null");

            for (String name : WINDOWS) {
                need(
                        window.getClass().getField(name).getInt(window) == -1,
                        "Unsupported window field must be SDK UNKNOWN, never CLOSED: " + name);
            }

            for (String name : List.of("fLWindow", "fRWindow", "rLWindow", "rRWindow")) {
                window.getClass().getField(name).getInt(window);
            }

            window.getClass().getField("fLWindow").setInt(window, 0);
            Object next = type.getMethod("getWindowStatus").invoke(provider);
            // Each read must return a fresh snapshot object, not the mutated previous one.
            @SuppressWarnings("ReferenceEquality")
            boolean fresh = next != window;
            need(
                    fresh && next.getClass().getField("fLWindow").getInt(next) == -1,
                    "Fresh unknown snapshots cannot leak caller mutations");
            Object door = type.getMethod("getDoorStatus").invoke(provider);
            need(
                    door == null,
                    "Preserve the OEM explicit door null guard; do not synthesize unsupported door"
                            + " actions");
            need(
                    type.getMethod("getGearStatus").invoke(provider).toString().equals("Unknown"),
                    "Unsupported gear must use SDK Unknown enum");
        }

        System.out.println(
                "PASS actual provider getter bodies: 21 UNKNOWN window fields, preserved door null"
                        + " guard, Unknown gear, independent snapshots");
    }

    static void oem(Path launcher, Path setting) throws Exception {
        need(
                FilesSupport.hash(launcher)
                        .equals("93f4bea56530bf2de8634b7898fd4275a3c8985ff0392fdc58607ef8bc5b2441"),
                "Exact NXP Launcher donor required");
        need(
                FilesSupport.hash(setting)
                        .equals("be5da5827f8b8710f9a73e73efa64203c29f39af5e3a32528c256c6042121de5"),
                "Exact NXP CAN SDK donor required");
        checkLauncherConsumer(launcher);
        var sdk =
                new DexBackedDexFile(
                        Opcodes.forApi(28), NativeCore.entries(setting).get("classes2.dex"));

        for (String name : List.of("WindowStatus", "DoorStatus")) {
            var cls =
                    sdk.getClasses().stream()
                            .filter(c -> c.getType().equals("Lcom/qinggan/canbus/" + name + ";"))
                            .findFirst()
                            .orElseThrow();
            var fields = new TreeSet<String>();

            for (var field : cls.getInstanceFields()) {
                need(field.getType().equals("I"), "Unexpected typed SDK field");
                fields.add(field.getName());
            }

            need(
                    fields.equals(name.equals("WindowStatus") ? WINDOWS : DOORS),
                    "Exact SDK field inventory differs");

            if (name.equals("DoorStatus")) {
                checkDoorConstructor(cls);
            }

            String constant = name.equals("WindowStatus") ? "UNKNOWN" : "DOOR_STATUS_UNKNOWN";
            boolean unknown = false;

            for (var field : cls.getStaticFields()) {
                if (field.getName().equals(constant)) {
                    unknown =
                            field.getInitialValue() instanceof IntEncodedValue i
                                    && i.getValue() == -1;
                }
            }

            need(unknown, "SDK unknown constant must be -1");
        }

        var gear =
                sdk.getClasses().stream()
                        .filter(c -> c.getType().equals("Lcom/qinggan/canbus/GearState;"))
                        .findFirst()
                        .orElseThrow();
        need(
                java.util.stream.StreamSupport.stream(gear.getStaticFields().spliterator(), false)
                        .anyMatch(f -> f.getName().equals("Unknown")),
                "SDK GearState.Unknown required");
        System.out.println(
                "PASS pinned OEM DEX: entire setupComponentState CAN getter/read inventory and"
                        + " exact SDK UNKNOWN fields");
    }

    static void checkLauncherConsumer(Path launcher) throws Exception {
        var launcherDex =
                new DexBackedDexFile(
                        Opcodes.forApi(28), NativeCore.entries(launcher).get("classes.dex"));
        var bar =
                launcherDex.getClasses().stream()
                        .filter(c ->
                                c.getType()
                                        .equals(
                                                "Lcom/qinggan/app/launcher/hiboard/VehicleHiboardBar;"))
                        .findFirst()
                        .orElseThrow();
        var method =
                java.util.stream.StreamSupport.stream(bar.getMethods().spliterator(), false)
                        .filter(m -> m.getName().equals("setupComponentState"))
                        .findFirst()
                        .orElseThrow();
        var calls = new java.util.TreeMap<String, Integer>();
        var reads = new TreeSet<String>();

        for (var instruction : method.getImplementation().getInstructions()) {
            if (instruction instanceof ReferenceInstruction r) {
                if (r.getReference() instanceof MethodReference m
                        && m.getDefiningClass().equals("Lcom/qinggan/canbus/CanBusManager;")) {
                    calls.merge(m.getName(), 1, Integer::sum);
                }

                if (r.getReference() instanceof FieldReference f
                        && f.getDefiningClass().startsWith("Lcom/qinggan/canbus/")
                        && instruction.getOpcode().name.startsWith("iget")) {
                    reads.add(f.getDefiningClass() + "->" + f.getName());
                }
            }
        }

        need(
                calls.equals(
                        Map.of(
                                "getWindowStatus",
                                2,
                                "getGearStatus",
                                2,
                                "getDoorStatus",
                                1,
                                "getVehicleState",
                                8)),
                "All setupComponentState CAN getters must remain audited: " + calls);
        need(
                reads.equals(
                        Set.of(
                                "Lcom/qinggan/canbus/WindowStatus;->fLWindow",
                                "Lcom/qinggan/canbus/WindowStatus;->fRWindow",
                                "Lcom/qinggan/canbus/WindowStatus;->rLWindow",
                                "Lcom/qinggan/canbus/WindowStatus;->rRWindow",
                                "Lcom/qinggan/canbus/DoorStatus;->fLDoorLockStatus")),
                "Consumer object dereference inventory differs: " + reads);
    }

    static void checkDoorConstructor(org.jf.dexlib2.iface.ClassDef cls) {
        var constructor =
                java.util.stream.StreamSupport.stream(cls.getDirectMethods().spliterator(), false)
                        .filter(m ->
                                m.getName().equals("<init>") && m.getParameterTypes().isEmpty())
                        .findFirst()
                        .orElseThrow();
        var registers = new java.util.HashMap<Integer, Integer>();
        var unknownFields = new TreeSet<String>();

        for (var instruction : constructor.getImplementation().getInstructions()) {
            if (instruction
                            instanceof
                            org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction literal
                    && instruction
                            instanceof org.jf.dexlib2.iface.instruction.OneRegisterInstruction r) {
                registers.put(r.getRegisterA(), literal.getNarrowLiteral());
            }

            if (instruction.getOpcode().name.equals("iput")
                    && instruction instanceof ReferenceInstruction r
                    && r.getReference() instanceof FieldReference f
                    && instruction
                            instanceof
                            org.jf.dexlib2.iface.instruction.TwoRegisterInstruction values
                    && Integer.valueOf(-1).equals(registers.get(values.getRegisterA()))) {
                unknownFields.add(f.getName());
            }
        }

        need(
                unknownFields.equals(DOORS),
                "Actual SDK DoorStatus constructor must initialize every field to UNKNOWN");
    }

    public static void main(String[] args) throws Exception {
        behavior(Path.of(args[0]), Path.of(args[1]));

        if (args.length == 4) {
            oem(Path.of(args[2]), Path.of(args[3]));
        }
    }
}
