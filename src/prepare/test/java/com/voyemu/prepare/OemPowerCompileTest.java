package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.reference.MethodReference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

/** Compile the real power implementation against only pinned OEM donor signatures. */
public final class OemPowerCompileTest {
    static void check(boolean ok) {
        if (!ok) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), work = Path.of(args[1]);
        Path component =
                root.resolve("profiles/free-11-chinese-20260731-7.3.1/components/oem-power");

        check(curtainPowerCalls(root) == 6);
        var config = Json.object(Json.parse(Files.readString(component.resolve("component.json"))));
        var donor =
                DexOperations.donor(
                        root.resolve(
                                ".local/profiles/free-11-chinese-20260731-7.3.1/originals/framework/QGAPI.jar"),
                        Json.obj(config, "donorDex"),
                        work.resolve("donor"),
                        30,
                        "");
        check(donor.descriptors().size() == 8);
        AudioPolicyConsumerGuard.verifyHiddenApiLayout(Files.readAllBytes(donor.dex()));
        var original =
                DexOperations.dex(
                        root.resolve(
                                ".local/profiles/free-11-chinese-20260731-7.3.1/originals/framework/QGAPI.jar"),
                        "classes.dex",
                        30);
        var before = new TreeMap<String, ClassDef>();

        for (var c : original.getClasses()) {
            if (donor.descriptors().contains(c.getType())) {
                before.put(c.getType(), c);
            }
        }

        for (var c :
                DexFileFactory.loadDexFile(donor.dex().toFile(), Opcodes.forApi(30)).getClasses()) {
            check(
                    FrameworkDexMaterializer.semantic(c)
                            .equals(FrameworkDexMaterializer.semantic(before.get(c.getType()))));
        }

        var signal =
                DexOperations.donor(
                        root.resolve(
                                ".local/profiles/free-11-chinese-20260731-7.3.1/originals/MusicConcate.apk"),
                        Json.obj(config, "secondDonorDex"),
                        work.resolve("second-donor"),
                        30,
                        "");
        check(
                signal.descriptors().size() == 27
                        && signal.descriptors().stream()
                                .allMatch(c -> c.startsWith("Lcom/qinggan/carsignal/")));
        Path source = work.resolve("source");
        FilesSupport.copyTree(component, source);
        ComponentSources.copy(
                Json.obj(config, "sourceFiles"),
                source,
                resource ->
                        Files.readAllBytes(root.resolve(resource.substring("voyemu/".length()))));
        Path output =
                DexOperations.javaDex(
                        source.resolve("src"),
                        null,
                        List.of(donor.signatures(), signal.signatures()),
                        root.resolve(".local/toolchains/java-sdk/platforms/android-34/android.jar"),
                        work.resolve("compiled"),
                        28,
                        source.resolve("src/compile-only"));

        try (var loader =
                new java.net.URLClassLoader(
                        new java.net.URL[] {work.resolve("compiled/classes").toUri().toURL()})) {
            checkDisplayTarget(loader.loadClass("com.voyemu.oem.ScreenDisplayTarget"));
            checkPowerState(loader.loadClass("com.voyemu.oem.ScreenPowerState"));
        }

        checkPowerStub(output, donor);
        check(carSignalMethods(output, signal) == 76);
        System.out.println(
                "Free A11 7.3.1 OEM power donor eight classes, car signal donor 27 classes, all 22"
                        + " power and 76 car signal ABI methods compiled and D8 validated");
    }

    /** The methods of the donor car signal interface, each implemented by the own CarSignalStub. */
    static int carSignalMethods(Path output, DexOperations.Donor donor) throws Exception {
        ClassDef own = null, declared = null;

        for (var c :
                DexFileFactory.loadDexFile(
                                output.resolve("classes.dex").toFile(), Opcodes.forApi(30))
                        .getClasses()) {
            if (c.getType().equals("Lcom/voyemu/oem/CarSignalStub;")) {
                own = c;
            }
        }

        for (var c :
                DexFileFactory.loadDexFile(donor.dex().toFile(), Opcodes.forApi(30)).getClasses()) {
            if (c.getType().equals("Lcom/qinggan/carsignal/ICarSignalService;")) {
                declared = c;
            }
        }

        check(own.getSuperclass().equals("Lcom/qinggan/carsignal/ICarSignalService$Stub;"));
        var implemented = new TreeSet<String>();

        for (var m : own.getVirtualMethods()) {
            implemented.add(
                    m.getName() + String.join("", m.getParameterTypes()) + m.getReturnType());
        }

        int methods = 0;

        for (var m : declared.getVirtualMethods()) {
            check(
                    implemented.contains(
                            m.getName()
                                    + String.join("", m.getParameterTypes())
                                    + m.getReturnType()));
            methods++;
        }

        return methods;
    }

    static int curtainPowerCalls(Path root) throws Exception {
        int curtainPowerCalls = 0;

        for (var c :
                DexOperations.dex(
                                root.resolve(
                                        ".local/profiles/free-11-chinese-20260731-7.3.1/originals/Hiboard.apk"),
                                "classes.dex",
                                30)
                        .getClasses()) {
            if (!Set.of(
                            "Lcom/qinggan/app/hiboard/system/HiboardBar;",
                            "Lcom/qinggan/app/hiboard/system/SecondHiboardBar;")
                    .contains(c.getType())) {
                continue;
            }

            for (var method : c.getMethods()) {
                if (method.getImplementation() != null) {
                    for (var instruction : method.getImplementation().getInstructions()) {
                        if (instruction instanceof ReferenceInstruction r
                                && r.getReference() instanceof MethodReference call
                                && call.getDefiningClass()
                                        .equals("Lcom/qinggan/system/PowerManager;")
                                && Set.of(
                                                "enterScreenOff",
                                                "exitScreenOff",
                                                "isScreenOff",
                                                "getAVNStatus")
                                        .contains(call.getName())) {
                            check(call.getParameterTypes().equals(List.of("I")));

                            if (call.getName().equals("enterScreenOff")
                                    || call.getName().equals("exitScreenOff")) {
                                curtainPowerCalls++;
                            }
                        }
                    }
                }
            }
        }

        return curtainPowerCalls;
    }

    static void checkDisplayTarget(Class<?> target) throws Exception {
        var match =
                target.getDeclaredMethod(
                        "matches", int.class, int.class, String.class, int.class, int.class);

        match.setAccessible(true);
        check((Boolean) match.invoke(null, 1, 0, "Built-in Screen", 1920, 720));
        check((Boolean) match.invoke(null, 2, 2, "Emulator2D", 1920, 720));
        check((Boolean) match.invoke(null, 2, 1, "Emulator 2D Display", 1920, 720));

        for (var values :
                List.<Object[]>of(
                        new Object[] {2, 0, "Emulator2D", 1920, 720},
                        new Object[] {2, 2, "Emulator2D fake", 1920, 720},
                        new Object[] {2, 2, "Emulator 2D Display", 1920, 1080},
                        new Object[] {2, 2, null, 1920, 720},
                        new Object[] {1, 2, "Emulator2D", 1920, 720},
                        new Object[] {3, 0, "Built-in Screen", 1920, 720})) {
            check(!(Boolean) match.invoke(null, values));
        }

        var select =
                target.getDeclaredMethod(
                        "select", int.class, int[].class, String[].class, int[].class, int[].class);
        select.setAccessible(true);
        check(
                (Integer)
                                select.invoke(
                                        null,
                                        1,
                                        new int[] {0, 1},
                                        new String[] {"Built-in Screen", "Emulator 2D Display"},
                                        new int[] {1920, 1920},
                                        new int[] {720, 720})
                        == 0);
        check(
                (Integer)
                                select.invoke(
                                        null,
                                        2,
                                        new int[] {0, 1},
                                        new String[] {"Built-in Screen", "Emulator 2D Display"},
                                        new int[] {1920, 1920},
                                        new int[] {720, 720})
                        == 1);
        check(
                (Integer)
                                select.invoke(
                                        null,
                                        2,
                                        new int[] {2, 3},
                                        new String[] {"Emulator2D", "Emulator 2D Display"},
                                        new int[] {1920, 1920},
                                        new int[] {720, 720})
                        == -1);
        check(
                (Integer)
                                select.invoke(
                                        null,
                                        1,
                                        new int[] {0, 0},
                                        new String[] {"Built-in Screen", "Built-in Screen"},
                                        new int[] {1920, 1920},
                                        new int[] {720, 720})
                        == -1);
    }

    static void checkPowerState(Class<?> stateClass) throws Exception {
        var constructor = stateClass.getDeclaredConstructor();

        constructor.setAccessible(true);
        var state = constructor.newInstance();
        var change = stateClass.getDeclaredMethod("setOff", int.class, boolean.class);
        change.setAccessible(true);
        var status = stateClass.getDeclaredMethod("status");
        status.setAccessible(true);
        check(Arrays.equals((int[]) status.invoke(state), new int[] {1, 1}));
        change.invoke(state, 1, true);
        check(Arrays.equals((int[]) status.invoke(state), new int[] {5, 1}));
        change.invoke(state, 2, true);
        check(Arrays.equals((int[]) status.invoke(state), new int[] {5, 5}));
        change.invoke(state, 1, false);
        check(Arrays.equals((int[]) status.invoke(state), new int[] {1, 5}));
        change.invoke(state, 2, false);
        check(Arrays.equals((int[]) status.invoke(state), new int[] {1, 1}));
        boolean rejected = false;

        try {
            change.invoke(state, 3, true);
        } catch (java.lang.reflect.InvocationTargetException expected) {
            rejected = expected.getCause() instanceof IllegalArgumentException;
        }

        check(rejected);
    }

    static void checkPowerStub(Path output, DexOperations.Donor donor) throws Exception {
        var classes = new TreeMap<String, ClassDef>();

        for (var c :
                DexFileFactory.loadDexFile(
                                output.resolve("classes.dex").toFile(), Opcodes.forApi(30))
                        .getClasses()) {
            classes.put(c.getType(), c);
        }

        ClassDef power = classes.get("Lcom/voyemu/oem/PowerManagerStub;");
        check(power.getSuperclass().equals("Lcom/qinggan/system/IPowerManagerService$Stub;"));
        var implemented = new TreeSet<String>();

        for (var m : power.getVirtualMethods()) {
            implemented.add(m.getName());
        }

        ClassDef service = null;

        for (var c :
                DexFileFactory.loadDexFile(donor.dex().toFile(), Opcodes.forApi(30)).getClasses()) {
            if (c.getType().equals("Lcom/qinggan/system/IPowerManagerService;")) {
                service = c;
            }
        }

        int methods = 0;

        for (var m : service.getVirtualMethods()) {
            check(implemented.contains(m.getName()));
            methods++;
        }

        check(methods == 22);
        check(classes.keySet().stream().noneMatch(s -> s.startsWith("Lcom/qinggan/")));
    }
}
