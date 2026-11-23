package com.voyemu.prepare;

import org.jf.dexlib2.AccessFlags;
import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.reference.FieldReference;
import org.jf.dexlib2.iface.value.IntEncodedValue;

import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.TreeSet;

import javax.tools.ToolProvider;

/** Effective profile sources preserve the OEM inherited, per-component pre-frame cache. */
public final class CanbusSeatBeltTest {
    private static final String PROFILE = "free-11-chinese-20260731-7.3.1";
    private static final String BASE = "com.qinggan.canbus.service.BaseCanBusComponent";
    private static final String SEAT = "Lcom/qinggan/canbus/SeatBelt;";
    private static final List<String> FIELDS =
            List.of(
                    "driverSeatBeltState",
                    "passengerSeatBeltState",
                    "secondLeftSeatBeltState",
                    "secondMidSeatBeltState",
                    "secondRightSeatBeltState");

    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    // The contract includes cache identity across reads and between components.
    @SuppressWarnings("ReferenceEquality")
    private static void source(Path root, Path work, String profile, Path dependencies)
            throws Exception {
        Path component = root.resolve("profiles/" + profile + "/components/canbus");
        var definition =
                Json.object(Json.parse(Files.readString(component.resolve("component.json"))));
        Path stage = work.resolve(profile + "/source");

        ComponentSources.copy(
                Json.obj(definition, "sourceFiles"),
                stage,
                resource ->
                        Files.readAllBytes(root.resolve(resource.substring("voyemu/".length()))));

        if (definition.containsKey("sourceEdits")) {
            String name = Json.str(definition, "sourceEdits");
            Files.copy(component.resolve(name), stage.resolve(name));
        }

        ComponentSources.editDeclared(definition, stage);
        Path classes = work.resolve(profile + "/classes");
        Files.createDirectories(classes);
        var compiler = ToolProvider.getSystemJavaCompiler();
        check(compiler != null, "The normal source-test JDK is required");
        check(
                compiler.run(
                                null,
                                null,
                                null,
                                "--release",
                                "25",
                                "-proc:none",
                                "-cp",
                                dependencies.toString(),
                                "-d",
                                classes.toString(),
                                stage.resolve("src/" + BASE.replace('.', '/') + ".java").toString())
                        == 0,
                "Compile the real effective Base, not a copied getter");

        try (var loader =
                new URLClassLoader(
                        new java.net.URL[] {classes.toUri().toURL(), dependencies.toUri().toURL()},
                        null)) {
            Class<?> type = Class.forName(BASE, true, loader);
            var getter = type.getMethod("getSeatBeltStatus");
            Object owner = type.getConstructor().newInstance();
            Object first = getter.invoke(owner);

            if (!profile.equals(PROFILE)) {
                check(first == null, "Other profiles retain their existing behavior: " + profile);

                return;
            }

            check(first != null, "The OEM general superclass supplies a non-null pre-frame object");
            check(getter.invoke(owner) == first, "Repeated reads retain the OEM cache identity");

            for (String field : FIELDS) {
                check(
                        first.getClass().getField(field).getInt(first) == 0,
                        "OEM Java default " + field);
            }

            first.getClass().getField(FIELDS.get(0)).setInt(first, 1);
            check(getter.invoke(owner) == first, "Reading must not replace a populated cache");
            check(
                    first.getClass().getField(FIELDS.get(0)).getInt(first) == 1,
                    "Retain cached state");
            Object other = getter.invoke(type.getConstructor().newInstance());
            check(other != first, "The cache belongs to a component, not a global static");
            check(
                    other.getClass().getField(FIELDS.get(0)).getInt(other) == 0,
                    "Independent default");
        }
    }

    private static void oem(Path root, Path original) throws Exception {
        var profile =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve("profiles/" + PROFILE + "/profile.json"))));

        check(
                FilesSupport.hash(original)
                        .equals(
                                Json.str(
                                        Json.obj(Json.obj(profile, "original"), "CanBusService"),
                                        "sha256")),
                "Pinned current OEM donor required");
        Map<String, ClassDef> classes = new TreeMap<>();
        var container = DexFileFactory.loadDexContainer(original.toFile(), Opcodes.forApi(30));

        for (String entry : container.getDexEntryNames()) {
            for (var type : container.getEntry(entry).getDexFile().getClasses()) {
                check(classes.put(type.getType(), type) == null, "Unique donor class");
            }
        }

        checkSeatShape(classes.get(SEAT));
        checkInheritedGetter(classes);
        System.out.println(
                "OEM SeatBelt inheritance, Parcelable shape, zero defaults and generic constants"
                        + " verified");
    }

    private static void checkSeatShape(ClassDef seat) {
        check(
                seat != null && seat.getInterfaces().contains("Landroid/os/Parcelable;"),
                "OEM Parcelable");
        var fields = new TreeSet<String>();
        Map<String, Integer> constants = new TreeMap<>();

        for (var field : seat.getFields()) {
            if (AccessFlags.STATIC.isSet(field.getAccessFlags())) {
                if (field.getInitialValue() instanceof IntEncodedValue number) {
                    constants.put(field.getName(), number.getValue());
                }
            } else {
                check(
                        field.getType().equals("I")
                                && AccessFlags.PUBLIC.isSet(field.getAccessFlags()),
                        "OEM public integer field");
                fields.add(field.getName());
            }
        }

        check(fields.equals(new TreeSet<>(FIELDS)), "Exact five OEM fields");
        check(
                constants.equals(
                        Map.of(
                                "SEATBELT_STATE_PULLED",
                                0,
                                "SEATBELT_STATE_PUSHED",
                                1,
                                "SEATBELT_STATE_UNSUPPORT",
                                -1)),
                "Do not invent a QNX-to-generic encoding");
        boolean constructor = false, parcel = false;

        for (var method : seat.getMethods()) {
            if (method.getName().equals("<init>") && method.getParameterTypes().isEmpty()) {
                var opcodes = new ArrayList<String>();

                for (var instruction : method.getImplementation().getInstructions()) {
                    opcodes.add(instruction.getOpcode().name());
                }

                check(
                        opcodes.equals(List.of("INVOKE_DIRECT", "RETURN_VOID")),
                        "Constructor leaves actual zero defaults");
                constructor = true;
            }

            if (method.getName().equals("writeToParcel")) {
                var order = new ArrayList<String>();

                for (var instruction : method.getImplementation().getInstructions()) {
                    if (instruction instanceof ReferenceInstruction reference
                            && reference.getReference() instanceof FieldReference field
                            && field.getDefiningClass().equals(SEAT)) {
                        order.add(field.getName());
                    }
                }

                check(order.equals(FIELDS), "Preserve Parcelable field order");
                parcel = true;
            }
        }

        check(constructor && parcel, "Actual constructor and serializer required");
    }

    private static void checkInheritedGetter(Map<String, ClassDef> classes) {
        String general = "Lcom/qinggan/canbus/service/protocol/general/CanBusComponentImpl;";

        for (String child :
                List.of(
                        "dongfeng/DongfengCanBusComponentImpl",
                        "dongfeng_h97c/DongfengH97CCanBusComponentImpl")) {
            check(
                    general.equals(
                            classes.get("Lcom/qinggan/canbus/service/protocol/" + child + ";")
                                    .getSuperclass()),
                    "The real Dongfeng inherits general CAN");
        }

        boolean getter = false;

        for (var method : classes.get(general).getMethods()) {
            if (method.getName().equals("getSeatBeltStatus")
                    && method.getReturnType().equals(SEAT)) {
                var opcodes = new ArrayList<String>();

                for (var instruction : method.getImplementation().getInstructions()) {
                    opcodes.add(instruction.getOpcode().name());
                }

                check(
                        opcodes.equals(
                                List.of(
                                        "IGET_OBJECT",
                                        "IF_NEZ",
                                        "NEW_INSTANCE",
                                        "INVOKE_DIRECT",
                                        "IPUT_OBJECT",
                                        "IGET_OBJECT",
                                        "RETURN_OBJECT")),
                        "OEM lazy cached object contract");
                getter = true;
            }
        }

        check(getter, "Inherited getter exists");
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 3 && args[0].equals("--oem")) {
            oem(Path.of(args[1]).toRealPath(), Path.of(args[2]).toRealPath());

            return;
        }

        check(args.length == 4, "ROOT WORK QINGGAN11_FIXTURE_CLASSES QINGGAN09_FIXTURE_CLASSES");
        Path root = Path.of(args[0]).toRealPath(), work = Path.of(args[1]);
        Path q11 = Path.of(args[2]).toRealPath(), q09 = Path.of(args[3]).toRealPath();

        for (String profile :
                List.of(
                        PROFILE,
                        "free-11-chinese-20250710-6.11.1",
                        "free-11-russian-20260318-rc1")) {
            source(root, work, profile, q11);
        }

        source(root, work, "free-09-chinese-20260106-6.0.3", q09);
        System.out.println(
                "Effective 731 source-edit pre-frame cache PASS; other three profile APIs"
                        + " unchanged");
    }
}
