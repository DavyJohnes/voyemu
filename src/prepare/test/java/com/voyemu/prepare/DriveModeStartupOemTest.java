package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.NarrowLiteralInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.RegisterRangeInstruction;
import org.jf.dexlib2.iface.instruction.TwoRegisterInstruction;
import org.jf.dexlib2.iface.reference.FieldReference;
import org.jf.dexlib2.iface.reference.MethodReference;
import org.jf.dexlib2.iface.reference.StringReference;

import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

import javax.tools.ToolProvider;

/**
 * The BCM PEPS power mode handler of the OEM CanBusService of one firmware family and the own
 * PepsPowerMode of that family read the same QGSettings, account and vehicle sources and send the
 * same commands with the same literal values and restored defaults; on the own handler a guest
 * starts in Eco whatever was saved, a logged-in account starts in its saved Comfort with no Eco
 * before it, a repeated power mode sends nothing, H97C publishes the drive mode with its linked
 * ambient light colour, and NXP sends the separate energy mode command for a guest.
 *
 * <p>Arguments: ROOT WORK, then OEM_APK PEPS_POWER_MODE_SOURCE OEM_CLASS FAMILY per firmware, where
 * FAMILY is dongfeng, dongfeng-nxp or dongfeng-h97c.
 */
public final class DriveModeStartupOemTest {
    /**
     * Other signals of frame 0x2C1 that the CAN model of the guest does not carry, by their
     * VehicleState names: remote mode, child lock, follow me home, mirror folding, approach unlock
     * and away lock.
     */
    static final Set<String> NOT_MODELED =
            Set.of(
                    "BCM_REMOTE_MODE",
                    "CHILD_LOCK_CONTROL",
                    "ComingHomeFunction",
                    "RVMFunction",
                    "APPROACH_UNLOCK",
                    "AWAY_LOCK");

    static final String STATE = "Lcom/qinggan/canbus/VehicleState;";
    static final String ALIASES = "Lcom/qinggan/canbus/DFVehicleState;";

    static void need(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    // ------------------------------------------------------------------
    // OEM bytecode
    // ------------------------------------------------------------------

    /**
     * Linear symbolic reading of one OEM method: constants, enum names and strings per register.
     */
    static final class Reader {
        final DexClasses classes;
        final String owner;
        final Map<String, String> aliases;
        final List<String> tokens = new ArrayList<>();
        final Set<String> visited = new LinkedHashSet<>();

        Reader(DexClasses classes, String owner) {
            this.classes = classes;
            this.owner = owner;
            this.aliases = aliases(classes);
        }

        /** DFVehicleState constants name VehicleState constants, sometimes under another name. */
        static Map<String, String> aliases(DexClasses classes) {
            Map<String, String> result = new HashMap<>();

            if (classes.entry(ALIASES) == null) {
                return result;
            }

            for (Method method : classes.get(ALIASES).getMethods()) {
                if (!method.getName().equals("<clinit>") || method.getImplementation() == null) {
                    continue;
                }

                String last = null;

                for (Instruction i : method.getImplementation().getInstructions()) {
                    if (i.getOpcode() == Opcode.SGET_OBJECT) {
                        last =
                                ((FieldReference) ((ReferenceInstruction) i).getReference())
                                        .getName();
                    } else if (i.getOpcode() == Opcode.SPUT_OBJECT && last != null) {
                        result.put(
                                ((FieldReference) ((ReferenceInstruction) i).getReference())
                                        .getName(),
                                last);
                    }
                }
            }

            return result;
        }

        Method method(String name, String type) {
            for (Method method : classes.get(type).getMethods()) {
                if (method.getName().equals(name) && method.getImplementation() != null) {
                    return method;
                }
            }

            return null;
        }

        static List<Integer> registers(Instruction i) {
            List<Integer> result = new ArrayList<>();

            if (i instanceof RegisterRangeInstruction range) {
                for (int r = 0; r < range.getRegisterCount(); r++) {
                    result.add(range.getStartRegister() + r);
                }
            } else if (i instanceof FiveRegisterInstruction five) {
                int[] all = {
                    five.getRegisterC(),
                    five.getRegisterD(),
                    five.getRegisterE(),
                    five.getRegisterF(),
                    five.getRegisterG()
                };

                for (int r = 0; r < five.getRegisterCount(); r++) {
                    result.add(all[r]);
                }
            }

            return result;
        }

        static String value(Object value) {
            return value instanceof Integer ? value.toString() : "var";
        }

        static String text(Object value, String prefix) {
            return value instanceof String s && s.startsWith(prefix)
                    ? s.substring(prefix.length())
                    : null;
        }

        Object fieldValue(FieldReference field) {
            String name = field.getName();

            if (field.getType().equals(STATE)) {
                return "enum:"
                        + (field.getDefiningClass().equals(ALIASES)
                                ? aliases.getOrDefault(name, name)
                                : name);
            }

            return field.getDefiningClass().endsWith("/VehicleOnlineState;")
                    ? "online:" + name
                    : null;
        }

        void read(Method method, int depth) {
            if (!visited.add(method.getName()) || depth > 4) {
                return;
            }

            Map<Integer, Object> regs = new HashMap<>();
            Object pending = null;

            for (Instruction i : method.getImplementation().getInstructions()) {
                Opcode op = i.getOpcode();

                if (i instanceof NarrowLiteralInstruction literal
                        && op.name().startsWith("CONST")
                        && op != Opcode.CONST_WIDE_16
                        && op != Opcode.CONST_WIDE_32) {
                    regs.put(
                            ((OneRegisterInstruction) i).getRegisterA(),
                            literal.getNarrowLiteral());
                } else if (op == Opcode.CONST_STRING || op == Opcode.CONST_STRING_JUMBO) {
                    regs.put(
                            ((OneRegisterInstruction) i).getRegisterA(),
                            "str:"
                                    + ((StringReference) ((ReferenceInstruction) i).getReference())
                                            .getString());
                } else if (op == Opcode.SGET_OBJECT) {
                    FieldReference field =
                            (FieldReference) ((ReferenceInstruction) i).getReference();
                    regs.put(((OneRegisterInstruction) i).getRegisterA(), fieldValue(field));
                } else if (op.name().startsWith("MOVE_RESULT")) {
                    regs.put(((OneRegisterInstruction) i).getRegisterA(), pending);
                    pending = null;
                } else if (op.name().startsWith("MOVE")
                        && i instanceof TwoRegisterInstruction two) {
                    regs.put(two.getRegisterA(), regs.get(two.getRegisterB()));
                } else if (op.name().startsWith("INVOKE")) {
                    MethodReference called =
                            (MethodReference) ((ReferenceInstruction) i).getReference();
                    List<Object> args = new ArrayList<>();

                    for (int r : registers(i)) {
                        args.add(regs.get(r));
                    }

                    Object receiver =
                            op.name().startsWith("INVOKE_STATIC") || args.isEmpty()
                                    ? null
                                    : args.remove(0);

                    pending = invoke(called, receiver, args, depth);
                } else if (op.setsRegister() && i instanceof OneRegisterInstruction one) {
                    regs.put(one.getRegisterA(), null);
                }
            }
        }

        Object invoke(MethodReference called, Object receiver, List<Object> args, int depth) {
            String name = called.getName();
            String type = called.getDefiningClass();
            String state = args.isEmpty() ? null : text(args.get(0), "enum:");

            switch (name) {
                case "setVehicleState" -> {
                    if (state != null && args.size() == 2) {
                        tokens.add("set " + state + "=" + value(args.get(1)));
                    }

                    return null;
                }
                case "initVehicleLastStatus" -> {
                    String restored = text(args.get(1), "enum:");
                    tokens.add("init " + restored + "=" + value(args.get(2)));

                    return null;
                }
                case "getVehicleState" -> {
                    if (state != null) {
                        tokens.add("get " + state);
                    }

                    return null;
                }
                case "onVehicleStateChanged" -> {
                    if (state != null) {
                        tokens.add("publish " + state);
                    }

                    return null;
                }
                case "saveVehicleStateData" -> {
                    if (state != null) {
                        tokens.add("save " + state);
                    }

                    return null;
                }
                case "onAccStateChanged" -> {
                    tokens.add("acc");

                    return null;
                }
                case "setVehicleAndAirConditionBundleState" -> {
                    tokens.add("bundle");

                    return null;
                }
                case "AccountInfoBean" -> {
                    tokens.add("account");

                    return null;
                }
                case "getVehicleTypeID" -> {
                    tokens.add("vehicle-type");

                    return null;
                }
                case "readOnlineConfigItem" -> {
                    tokens.add("online " + text(args.get(0), "online:"));

                    return null;
                }
                case "toString", "name" -> {
                    String constant = text(receiver, "enum:");

                    return constant == null ? null : "name:" + constant;
                }
                case "valueOf" -> {
                    return type.equals("Ljava/lang/Integer;") ? args.get(0) : null;
                }
                default -> {}
            }

            if (type.equals("Landroid/os/Bundle;") && name.equals("putInt")) {
                tokens.add("put " + text(args.get(0), "name:") + "=" + value(args.get(1)));
            } else if (type.startsWith("Lcom/qinggan/provider/QGSettings$")) {
                String key = text(args.get(1), "str:");

                if (type.endsWith("$Global;")) {
                    tokens.add("qg-global " + key);
                } else {
                    tokens.add((name.startsWith("put") ? "qg-put " : "qg-get ") + key);
                }
            } else if (type.endsWith("/SystemProperties;") && name.equals("get")) {
                tokens.add("prop " + text(args.get(0), "str:"));
            } else if (name.equals("put") && type.matches("Ljava/util/(Hash)?Map;")) {
                if (state != null) {
                    tokens.add("cache " + state);
                }
            } else if (type.equals(owner)) {
                Method inner = method(name, owner);

                if (inner != null) {
                    read(inner, depth + 1);
                }
            }

            return null;
        }
    }

    static List<String> oemTokens(Path apk, String owner) throws Exception {
        Map<String, byte[]> archive = new TreeMap<>(NativeCore.entries(apk));
        DexClasses classes =
                new DexClasses(
                        archive,
                        archive.keySet().stream()
                                .filter(n -> n.matches("classes[0-9]*\\.dex"))
                                .sorted()
                                .toList(),
                        28);
        Reader reader = new Reader(classes, owner);
        Method handler = reader.method("onBCM_PEPSChangeData", owner);

        need(handler != null, "OEM onBCM_PEPSChangeData absent in " + owner);
        reader.read(handler, 0);
        List<String> result = new ArrayList<>();

        for (String token : reader.tokens) {
            String[] parts = token.split(" ", 2);
            String state = parts.length < 2 ? null : parts[1].split("=", 2)[0];

            if (state == null || !NOT_MODELED.contains(state)) {
                result.add(token);
            }
        }

        return result;
    }

    // ------------------------------------------------------------------
    // Own handler
    // ------------------------------------------------------------------

    /** The own handler of the family with a recording vehicle, loaded from its compiled sources. */
    static final class Port {
        final ClassLoader loader;
        final Set<String> stateNames;

        Port(ClassLoader loader, Set<String> stateNames) {
            this.loader = loader;
            this.stateNames = stateNames;
        }

        Object vehicle() throws Exception {
            return loader.loadClass("com.qinggan.canbus.service.RecordingVehicle")
                    .getConstructor(Set.class)
                    .newInstance(stateNames);
        }

        Object handler(Object vehicle) throws Exception {
            Class<?> base = loader.loadClass("com.qinggan.canbus.service.PowerModeVehicle");

            return loader.loadClass("com.qinggan.canbus.service.PepsPowerMode")
                    .getConstructor(base)
                    .newInstance(vehicle);
        }

        @SuppressWarnings("unchecked")
        static List<String> tokens(Object vehicle) throws Exception {
            return (List<String>) vehicle.getClass().getField("tokens").get(vehicle);
        }

        @SuppressWarnings("unchecked")
        static Map<String, Object> map(Object vehicle, String name) throws Exception {
            return (Map<String, Object>) vehicle.getClass().getField(name).get(vehicle);
        }

        static void set(Object vehicle, String name, Object value) throws Exception {
            vehicle.getClass().getField(name).set(vehicle, value);
        }

        static void power(Object handler, int power) throws Exception {
            handler.getClass().getMethod("onPowerMode", int.class).invoke(handler, power);
        }
    }

    static Port compile(Path root, Path work, String source, Set<String> stateNames)
            throws Exception {
        Path classes = work.resolve("classes");

        Files.createDirectories(classes);
        String fixtures = "src/prepare/test/fixtures/canbus-component/";
        List<String> arguments =
                new ArrayList<>(
                        List.of(
                                "--release",
                                "25",
                                "-proc:none",
                                "-nowarn",
                                "-d",
                                classes.toString(),
                                root.resolve(source).toString(),
                                root.resolve(
                                                "profiles/common/components/canbus/src/com/qinggan/canbus/service/PowerModeVehicle.java")
                                        .toString(),
                                root.resolve(
                                                "src/prepare/test/fixtures/drive-mode/com/qinggan/canbus/service/RecordingVehicle.java")
                                        .toString()));

        for (String fixture :
                List.of(
                        "android/content/ContentResolver.java",
                        "android/util/Log.java",
                        "dalvik/system/PathClassLoader.java")) {
            arguments.add(root.resolve(fixtures + fixture).toString());
        }

        need(
                ToolProvider.getSystemJavaCompiler()
                                .run(null, null, null, arguments.toArray(String[]::new))
                        == 0,
                "Own power mode handler must compile");

        return new Port(
                new URLClassLoader(
                        new java.net.URL[] {classes.toUri().toURL()},
                        DriveModeStartupOemTest.class.getClassLoader()),
                stateNames);
    }

    /** One power mode sequence of the own handler: ACC ON, ACC OFF and ACC ON again. */
    record Run(List<String> tokens, List<String> firstOn, List<String> secondOn) {}

    record Scenario(
            boolean guest,
            int account,
            int drive,
            int linked,
            int type,
            boolean wash,
            int matrix) {}

    static Run run(Port port, Scenario scenario) throws Exception {
        Object vehicle = port.vehicle();

        Port.set(vehicle, "guest", scenario.guest());
        Port.set(vehicle, "type", scenario.type());
        Port.set(vehicle, "matrix", scenario.matrix());
        Port.map(vehicle, "settings").put("VehicleAccountInfo", scenario.account());
        Port.map(vehicle, "settings").put("DRIVING_MODE_SET", scenario.drive());
        Port.map(vehicle, "global").put("vehicle_setting_drive_mode_atm", scenario.linked());
        Port.map(vehicle, "properties")
                .put("com.qinggan.manualwashmode.wash_car.state", scenario.wash() ? "on" : "off");
        Object handler = port.handler(vehicle);
        List<String> tokens = Port.tokens(vehicle);

        Port.power(handler, 2);
        List<String> firstOn = new ArrayList<>(tokens);
        Port.power(handler, 1);
        int second = tokens.size();
        Port.power(handler, 2);

        return new Run(
                new ArrayList<>(tokens),
                firstOn,
                new ArrayList<>(tokens.subList(second, tokens.size())));
    }

    // ------------------------------------------------------------------
    // Comparison
    // ------------------------------------------------------------------

    static String kind(String token) {
        return token.split(" ", 2)[0].split("=", 2)[0];
    }

    static String state(String token) {
        String[] parts = token.split(" ", 2);

        return parts.length < 2 ? "" : parts[1].split("=", 2)[0];
    }

    static String literal(String token) {
        String[] parts = token.split("=", 2);

        return parts.length < 2 ? null : parts[1];
    }

    /** Tokens without their values; init keeps its default as part of the restore. */
    static Set<String> plain(List<String> tokens) {
        Set<String> result = new TreeSet<>();

        for (String token : tokens) {
            result.add(
                    kind(token).equals("init") || kind(token).equals("acc")
                            ? token.replaceFirst("^acc=.*", "acc")
                            : token.replaceFirst("=.*$", ""));
        }

        return result;
    }

    /** OEM literal command values per command kind and state; "var" marks a computed value. */
    static Map<String, Set<String>> literals(List<String> tokens) {
        Map<String, Set<String>> result = new TreeMap<>();

        for (String token : tokens) {
            String kind = kind(token);

            if ((kind.equals("set") || kind.equals("put")) && literal(token) != null) {
                result.computeIfAbsent(kind + " " + state(token), k -> new TreeSet<>())
                        .add(literal(token));
            }
        }

        return result;
    }

    /** Restores with their defaults; the bytecode places branches out of source order. */
    static List<String> restores(List<String> tokens) {
        return tokens.stream().filter(t -> kind(t).equals("init")).sorted().toList();
    }

    public static void main(String[] args) throws Exception {
        need(args.length > 2 && (args.length - 2) % 4 == 0, "ROOT WORK and quadruples required");

        for (int i = 2; i < args.length; i += 4) {
            check(
                    Path.of(args[0]),
                    Path.of(args[1]).resolve(Integer.toString(i / 4)),
                    Path.of(args[i]),
                    args[i + 1],
                    args[i + 2],
                    args[i + 3]);
        }
    }

    static List<String> modeVariants(Port port, boolean h97c) throws Exception {
        List<String> own = new ArrayList<>();
        int[] types = h97c ? new int[] {134, 13401, 13402} : new int[] {134};

        for (int type : types) {
            for (int account : new int[] {1, 2}) {
                for (boolean guest : new boolean[] {true, false}) {
                    for (int drive = 1; drive <= 6; drive++) {
                        for (int linked : new int[] {0, 1}) {
                            for (boolean wash : new boolean[] {false, true}) {
                                for (int matrix : new int[] {0, 1}) {
                                    Run r =
                                            run(
                                                    port,
                                                    new Scenario(
                                                            guest, account, drive, linked, type,
                                                            wash, matrix));
                                    own.addAll(r.tokens());
                                }
                            }
                        }
                    }
                }
            }
        }

        return own;
    }

    static void check(Path root, Path work, Path apk, String source, String oemClass, String family)
            throws Exception {
        String owner = "L" + oemClass.replace('.', '/') + ";";
        boolean nxp = family.equals("dongfeng-nxp");
        boolean h97c = family.equals("dongfeng-h97c");

        need(
                family.equals("dongfeng") || nxp || h97c,
                "family dongfeng, dongfeng-nxp or dongfeng-h97c required");
        List<String> oem = oemTokens(apk, owner);
        Set<String> stateNames = new TreeSet<>();

        for (String token : oem) {
            if (Set.of("set", "put", "init", "get", "publish", "save", "cache")
                    .contains(kind(token))) {
                stateNames.add(state(token));
            }
        }

        Port port = compile(root, work, source, stateNames);
        List<String> own = modeVariants(port, h97c);
        List<String> loggedIn = run(port, new Scenario(false, 2, 2, 1, 134, false, 0)).firstOn();

        // Same sources and commands; one ACC ON restores each OEM state once with its default.
        need(
                plain(oem).equals(plain(own)),
                "OEM and own handler differ: OEM " + plain(oem) + " own " + plain(own));
        need(
                restores(oem).equals(restores(loggedIn)),
                "OEM restore defaults " + restores(oem) + " own " + restores(loggedIn));
        Map<String, Set<String>> oemLiterals = literals(oem);
        Map<String, Set<String>> ownLiterals = literals(own);

        for (var entry : oemLiterals.entrySet()) {
            Set<String> values = new TreeSet<>(entry.getValue());
            boolean computed = values.remove("var");
            Set<String> seen = ownLiterals.getOrDefault(entry.getKey(), Set.of());

            need(
                    seen.containsAll(values),
                    entry.getKey() + " literals " + values + " not sent: " + seen);
            need(
                    computed || values.containsAll(seen),
                    entry.getKey()
                            + " sends values beyond the OEM literals "
                            + values
                            + ": "
                            + seen);
        }

        // A guest starts in Eco although Comfort was saved; the energy mode comes first on NXP.
        List<String> guest = run(port, new Scenario(true, 1, 2, 1, 134, false, 0)).secondOn();
        int bundle = guest.indexOf("bundle");
        need(
                bundle > 0 && guest.subList(0, bundle).contains("put DRIVING_MODE_SET=1"),
                "guest ACC ON must send Eco in its first drive mode packet: " + guest);
        need(
                nxp
                        == (guest.indexOf("set IVI_SOC_MODESET=1") >= 0
                                && guest.indexOf("set IVI_SOC_MODESET=1") < bundle),
                "the separate energy mode command precedes the guest packet only on NXP: " + guest);

        // A logged-in account starts in its saved Comfort and never in Eco before it.
        List<String> comfort = run(port, new Scenario(false, 2, 2, 1, 134, false, 0)).secondOn();
        int first = comfort.indexOf("put DRIVING_MODE_SET=2");
        need(
                first >= 0
                        && comfort.subList(0, first).stream()
                                .noneMatch(t ->
                                        t.equals("put DRIVING_MODE_SET=1")
                                                || t.equals("set DRIVING_MODE_SET=1")),
                "logged-in ACC ON must send the saved Comfort first: " + comfort);
        need(
                comfort.stream().noneMatch(t -> t.startsWith("set IVI_SOC_MODESET")),
                "a logged-in account gets no separate energy mode command: " + comfort);

        // A repeated power mode sends nothing.
        Object vehicle = port.vehicle();
        Object handler = port.handler(vehicle);
        Port.power(handler, 2);
        int before = Port.tokens(vehicle).size();
        Port.power(handler, 2);
        List<String> repeated = Port.tokens(vehicle).subList(before, Port.tokens(vehicle).size());
        need(
                repeated.stream().allMatch(t -> t.startsWith("get ")),
                "a repeated power mode must not repeat the start: " + repeated);

        // H97C publishes the drive mode and selects the linked ambient light colour.
        int published = comfort.indexOf("publish DRIVING_MODE_SET=2");
        need(
                h97c
                        == (published > comfort.indexOf("bundle")
                                && comfort.indexOf("set VEHICLE_AMBIENT_LIGHT_COLOR=5")
                                        > published),
                "H97C must publish Comfort and select its linked colour after the packet: "
                        + comfort);
        need(
                run(port, new Scenario(true, 1, 2, 0, 134, false, 0)).secondOn().stream()
                        .noneMatch(t -> t.startsWith("set VEHICLE_AMBIENT_LIGHT_COLOR")),
                "an unlinked ambient light keeps its colour");
        System.out.println(
                "DriveModeStartupOemTest "
                        + family
                        + ": "
                        + plain(oem).size()
                        + " OEM sources and commands, "
                        + restores(oem).size()
                        + " restores, "
                        + oemLiterals.size()
                        + " literal commands match the own handler");
    }
}
