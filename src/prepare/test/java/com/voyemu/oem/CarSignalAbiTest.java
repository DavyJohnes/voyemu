package com.voyemu.oem;

import com.qinggan.carsignal.ICarSignalService;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Field;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.value.IntEncodedValue;
import org.jf.dexlib2.iface.value.StringEncodedValue;

import java.nio.file.Path;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

/**
 * The OEM qg.carsignal Binder ABI that the shared stub serves: the A9 donor, the A11 donors and the
 * A11 clients carry the descriptor, the transaction codes and the methods of the accepted A9 donor,
 * and the interface the stub implements declares exactly those methods.
 */
public final class CarSignalAbiTest {
    private static final String SERVICE = "Lcom/qinggan/carsignal/ICarSignalService;";

    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static Map<String, ClassDef> classes(Path file, String dex) throws Exception {
        var entry =
                DexFileFactory.loadDexContainer(file.toFile(), Opcodes.forApi(30)).getEntry(dex);
        Map<String, ClassDef> result = new TreeMap<>();

        check(entry != null, "missing " + dex + " of " + file);

        for (ClassDef type : entry.getDexFile().getClasses()) {
            result.put(type.getType(), type);
        }

        return result;
    }

    /** The descriptor, transaction codes and abstract methods of one OEM copy of the interface. */
    private static Set<String> abi(Map<String, ClassDef> classes, String origin) {
        ClassDef stub = classes.get("Lcom/qinggan/carsignal/ICarSignalService$Stub;");
        ClassDef service = classes.get(SERVICE);
        Set<String> result = new TreeSet<>();

        check(stub != null && service != null, "interface absent in " + origin);

        for (Field field : stub.getStaticFields()) {
            if (field.getInitialValue() instanceof IntEncodedValue value
                    && field.getName().startsWith("TRANSACTION_")) {
                result.add(field.getName() + "=" + value.getValue());
            } else if (field.getInitialValue() instanceof StringEncodedValue value
                    && field.getName().equals("DESCRIPTOR")) {
                result.add("DESCRIPTOR=" + value.getValue());
            }
        }

        for (Method method : service.getMethods()) {
            result.add(
                    method.getName()
                            + "("
                            + String.join("", method.getParameterTypes())
                            + ")"
                            + method.getReturnType());
        }

        return result;
    }

    private static String descriptor(Class<?> type) {
        if (type.isArray()) {
            return "[" + descriptor(type.getComponentType());
        }

        if (!type.isPrimitive()) {
            return "L" + type.getName().replace('.', '/') + ";";
        }

        return Map.of(
                        int.class, "I",
                        long.class, "J",
                        float.class, "F",
                        boolean.class, "Z",
                        void.class, "V")
                .get(type);
    }

    public static void main(String[] args) throws Exception {
        Path profiles = Path.of(args[0]).resolve(".local/profiles");
        Path a9 = profiles.resolve("free-09-chinese-20260106-6.0.3/originals"),
                a611 = profiles.resolve("free-11-chinese-20250710-6.11.1/originals"),
                a731 = profiles.resolve("free-11-chinese-20260731-7.3.1/originals");
        Set<String> reference = abi(classes(a9.resolve("QGIME.apk"), "classes.dex"), "A9 donor");
        Map<String, Set<String>> copies =
                Map.of(
                        "6.11.1 donor",
                        abi(classes(a611.resolve("MusicConcate.apk"), "classes2.dex"), "6.11.1"),
                        "7.3.1 donor",
                        abi(classes(a731.resolve("MusicConcate.apk"), "classes2.dex"), "7.3.1"),
                        "6.11.1 client",
                        abi(classes(a611.resolve("Setting.apk"), "classes.dex"), "6.11.1"),
                        "7.3.1 client",
                        abi(
                                classes(a731.resolve("framework/QGVehicle.jar"), "classes.dex"),
                                "7.3.1"));

        check(
                reference.contains("DESCRIPTOR=com.qinggan.carsignal.ICarSignalService")
                        && reference.contains("TRANSACTION_getMCUHardwareVersion=1")
                        && reference.contains("TRANSACTION_getCarType=68")
                        && reference.contains("TRANSACTION_registerCallback=46")
                        && reference.contains("TRANSACTION_getTboxMutePinState=76")
                        && reference.stream().filter(e -> e.startsWith("TRANSACTION_")).count()
                                == 76,
                "A9 donor descriptor and transaction codes 1..76");

        for (var copy : copies.entrySet()) {
            check(copy.getValue().equals(reference), copy.getKey() + " ABI differs from A9 donor");
        }

        Set<String> implemented = new TreeSet<>();

        for (java.lang.reflect.Method method : ICarSignalService.class.getDeclaredMethods()) {
            StringBuilder text = new StringBuilder(method.getName()).append('(');

            for (Class<?> parameter : method.getParameterTypes()) {
                text.append(descriptor(parameter));
            }

            implemented.add(text.append(')').append(descriptor(method.getReturnType())).toString());
        }

        reference.removeIf(e -> e.contains("="));
        check(implemented.equals(reference), "stub interface differs from the OEM methods");
        System.out.println(
                "qg.carsignal ABI: 76 transactions and methods of the A9 donor in both A11 donors"
                        + " and clients and in the stub interface");
    }
}
