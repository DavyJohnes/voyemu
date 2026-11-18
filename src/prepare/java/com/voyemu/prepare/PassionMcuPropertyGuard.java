package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11x;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.DexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.OneRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.TreeMap;

/** Only the original Monza factory's IProperty argument is decorated. */
final class PassionMcuPropertyGuard {
    static final String PROFILE = "passion-11-russian-20260403-2.1.4.80";
    static final String PIN = "5cd091682a3ba6aa054a0b545a2c5a853e3e5bff7129f7319ebe8d583eccea98";
    static final String OWNER = "Lcom/mega/car/CarServiceFactory;",
            BRIDGE = "Lcom/voyemu/vehicle/PassionMcuBridge;";
    static final String CONSTRUCTOR =
            "Lcom/mega/car/service/LegacyCarPropertyService;-><init>(Landroid/content/Context;Lcom/mega/car/hal/IProperty;)V";

    static void require(boolean value) {
        if (!value) {
            throw new IllegalArgumentException(
                    "PASSION_MCU_PROPERTY_CONTRACT: exact pinned Monza constructor required");
        }
    }

    static boolean selected(Method method) {
        return method.getDefiningClass().equals(OWNER)
                && method.getName().equals("createServiceList");
    }

    static Method route(Method method, boolean inverse) {
        require(
                method.getParameterTypes()
                                .equals(
                                        List.of(
                                                "Landroid/content/Context;",
                                                "Lcom/mega/car/hal/vehicle/VehicleHal;",
                                                "Lcom/mega/car/CarImpl$HalFactory;"))
                        && method.getReturnType().equals("Ljava/util/List;")
                        && method.getAccessFlags() == 1);
        var body = new MutableMethodImplementation(method.getImplementation());
        require(body.getRegisterCount() == 16 && body.getTryBlocks().isEmpty());
        int at = 0, index = -1, sites = 0;

        for (int n = 0; n < body.getInstructions().size(); n++) {
            var i = body.getInstructions().get(n);

            if (at == 97) {
                index = n;
            }

            if (i instanceof ReferenceInstruction r
                    && r.getReference().toString().equals(CONSTRUCTOR)) {
                require(i.getOpcode() == Opcode.INVOKE_DIRECT);
                var args = (FiveRegisterInstruction) i;
                require(
                        args.getRegisterCount() == 3
                                && args.getRegisterC() == 1
                                && args.getRegisterD() == 13
                                && args.getRegisterE() == 0
                                && at == (inverse ? 101 : 97));
                sites++;
            }

            at += i.getCodeUnits();
        }

        require(index >= 0 && sites == 1 && at == (inverse ? 145 : 141));

        if (inverse) {
            removeBridgeWrap(body, index);
        } else {
            body.addInstruction(
                    index,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_STATIC,
                            2,
                            13,
                            0,
                            0,
                            0,
                            0,
                            new ImmutableMethodReference(
                                    BRIDGE,
                                    "wrap",
                                    List.of(
                                            "Landroid/content/Context;",
                                            "Lcom/mega/car/hal/IProperty;"),
                                    "Lcom/mega/car/hal/IProperty;")));
            body.addInstruction(index + 1, new BuilderInstruction11x(Opcode.MOVE_RESULT_OBJECT, 0));
        }

        return new ImmutableMethod(
                method.getDefiningClass(),
                method.getName(),
                method.getParameters(),
                method.getReturnType(),
                method.getAccessFlags(),
                method.getAnnotations(),
                method.getHiddenApiRestrictions(),
                body);
    }

    private static void removeBridgeWrap(MutableMethodImplementation body, int index) {
        var first = body.getInstructions().get(index);

        require(
                first.getOpcode() == Opcode.INVOKE_STATIC
                        && first instanceof ReferenceInstruction referenceInstruction
                        && referenceInstruction
                                .getReference()
                                .toString()
                                .equals(
                                        BRIDGE
                                                + "->wrap(Landroid/content/Context;Lcom/mega/car/hal/IProperty;)Lcom/mega/car/hal/IProperty;"));
        var args = (FiveRegisterInstruction) first;
        require(
                args.getRegisterCount() == 2
                        && args.getRegisterC() == 13
                        && args.getRegisterD() == 0);
        var next = body.getInstructions().get(index + 1);
        require(
                next.getOpcode() == Opcode.MOVE_RESULT_OBJECT
                        && ((OneRegisterInstruction) next).getRegisterA() == 0);
        body.removeInstruction(index);
        body.removeInstruction(index);
    }

    static ImmutableDexFile rewrite(DexFile dex, boolean inverse) {
        var output = new ArrayList<ClassDef>();
        int count = 0;

        for (var c : dex.getClasses()) {
            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();

            for (var m : c.getDirectMethods()) {
                require(!selected(m));
                direct.add(m);
            }

            for (var m : c.getVirtualMethods()) {
                if (selected(m)) {
                    m = route(m, inverse);
                    count++;
                }

                virtual.add(m);
            }

            output.add(
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
                            virtual));
        }

        require(count == 1);

        return new ImmutableDexFile(Opcodes.forApi(30), output);
    }

    static final Set<String> SOURCES =
            Set.of(
                    "com/voyemu/vehicle/PassionMcuBridge.java",
                    "com/voyemu/vehicle/PassionMcuProperty.java",
                    "com/voyemu/vehicle/PassionMcuProtocol.java",
                    "com/voyemu/vehicle/PassionMcuProvider.java");

    static Path compileHelper(
            String profile, int api, Path apk, Path source, Path platform, Path work)
            throws Exception {
        require(
                PROFILE.equals(profile)
                        && api == 30
                        && FilesSupport.hash(apk).equals(PIN)
                        && !Files.exists(work));
        require(GuestJava.sources(source).equals(SOURCES));
        Path output =
                GuestJava.dex(new GuestJava.Unit(source, api), platform, work.resolve("java"));
        var dexes = FilesSupport.tree(output);
        require(dexes.keySet().equals(Set.of("classes.dex")));
        validateHelper(Files.readAllBytes(output.resolve("classes.dex")));

        return output.resolve("classes.dex");
    }

    static void validateHelper(byte[] helper) throws Exception {
        require(helper != null);
        var classes = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), helper).getClasses()) {
            classes.put(c.getType(), c);
        }

        require(
                classes.keySet()
                        .equals(
                                Set.of(
                                        BRIDGE,
                                        "Lcom/voyemu/vehicle/PassionMcuProtocol;",
                                        "Lcom/voyemu/vehicle/PassionMcuProvider;",
                                        "Lcom/voyemu/vehicle/PassionMcuProperty;",
                                        "Lcom/voyemu/vehicle/PassionMcuProperty$1;",
                                        "Lcom/voyemu/vehicle/PassionMcuProperty$Snapshot;")));
        int wraps = 0;

        for (var method : classes.get(BRIDGE).getMethods()) {
            if (method.getName().equals("wrap")) {
                require(
                        method.getAccessFlags() == (1 | 8 | 0x20000)
                                && method.getReturnType().equals("Lcom/mega/car/hal/IProperty;")
                                && method.getParameterTypes()
                                        .equals(
                                                List.of(
                                                        "Landroid/content/Context;",
                                                        "Lcom/mega/car/hal/IProperty;")));
                wraps++;
            }
        }

        require(wraps == 1);
        require(
                classes.get("Lcom/voyemu/vehicle/PassionMcuProvider;")
                        .getSuperclass()
                        .equals("Landroid/content/ContentProvider;"));
    }

    static void validate(String profile, int api, Path apk, byte[] dex) throws Exception {
        require(
                PROFILE.equals(profile)
                        && api == 30
                        && FilesSupport.hash(apk).equals(PIN)
                        && dex != null);
        rewrite(new DexBackedDexFile(Opcodes.forApi(30), dex), false);
    }

    static byte[] apply(String profile, int api, Path apk, byte[] dex) throws Exception {
        validate(profile, api, apk, dex);
        var original = new DexBackedDexFile(Opcodes.forApi(30), dex);
        byte[] patched = FrameworkDexMaterializer.serialize(rewrite(original, false));
        var restored = rewrite(new DexBackedDexFile(Opcodes.forApi(30), patched), true);
        var map = new TreeMap<String, ClassDef>();

        for (var c : restored.getClasses()) {
            map.put(c.getType(), c);
        }

        int count = 0;

        for (var c : original.getClasses()) {
            require(
                    FrameworkDexMaterializer.semantic(c)
                            .equals(FrameworkDexMaterializer.semantic(map.get(c.getType()))));
            count++;
        }

        require(count == map.size());

        return patched;
    }
}
