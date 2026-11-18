package com.voyemu.input;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.reference.MethodReference;
import org.jf.dexlib2.iface.value.IntEncodedValue;

import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;

/** Actual owner identity and numeric Binder/parcel ABI; no OEM implementation is copied. */
public final class FreeWheelAbiTest {
    private static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static String sha(Path file) throws Exception {
        return java.util.HexFormat.of()
                .formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(file)));
    }

    private static ClassDef type(Path owner, String dex, String name) throws Exception {
        var entry =
                DexFileFactory.loadDexContainer(owner.toFile(), Opcodes.forApi(30)).getEntry(dex);

        if (entry != null) {
            for (ClassDef value : entry.getDexFile().getClasses()) {
                if (value.getType().equals(name)) {
                    return value;
                }
            }
        }

        throw new AssertionError("missing actual owner class " + name);
    }

    private static Method method(ClassDef type, String name) {
        for (Method method : type.getMethods()) {
            if (method.getName().equals(name)) {
                return method;
            }
        }

        throw new AssertionError("missing actual owner method " + name);
    }

    private static List<String> parcelCalls(Method method) {
        List<String> result = new ArrayList<>();

        for (var instruction : method.getImplementation().getInstructions()) {
            if (instruction instanceof ReferenceInstruction
                    && ((ReferenceInstruction) instruction).getReference()
                            instanceof MethodReference) {
                MethodReference called =
                        (MethodReference) ((ReferenceInstruction) instruction).getReference();

                if (called.getDefiningClass().equals("Landroid/os/Parcel;")
                        && !called.getName().equals("recycle")) {
                    result.add(called.getName());
                }
            }
        }

        return result;
    }

    public static void main(String[] args) throws Exception {
        Path originals =
                Path.of(args[0])
                        .resolve(".local/profiles/free-11-chinese-20260731-7.3.1/originals");
        Path api = originals.resolve("framework/QGAPI.jar"),
                launcher = originals.resolve("Launcher.apk");

        check(sha(api).equals(FreeWheelBroker.OWNER_SHA256), "QGAPI owner pin");
        check(
                sha(launcher)
                        .equals("b3287150827e7fa51072bcbcba257539101cda8433c5f9d3a95f4ca905abd567"),
                "Launcher owner pin");
        ClassDef stub = type(api, "classes.dex", "Lcom/qinggan/system/IKeyManagerService$Stub;");
        int[] ids = {1, 2, 3, 4};
        String[] names = {
            "registerKeyManagerListener",
            "unregisterKeyManagerListener",
            "registerInterceptListener",
            "unregisterInterceptListener"
        };

        for (int i = 0; i < ids.length; i++) {
            boolean found = false;

            for (var field : stub.getStaticFields()) {
                if (field.getName().equals("TRANSACTION_" + names[i])) {
                    check(
                            ((IntEncodedValue) field.getInitialValue()).getValue() == ids[i],
                            "transaction ID");
                    found = true;
                }
            }

            check(found, "transaction field");
        }

        ClassDef iface = type(api, "classes.dex", "Lcom/qinggan/system/IKeyManagerService;");
        check(
                method(iface, "registerKeyManagerListener")
                        .getParameterTypes()
                        .toString()
                        .equals(
                                "[Landroid/os/IBinder;, Lcom/qinggan/system/IKeyManagerCallback;,"
                                        + " [I, Ljava/lang/String;, I, I]"),
                "registration signature");
        ClassDef callback =
                type(api, "classes.dex", "Lcom/qinggan/system/IKeyManagerCallback$Stub;");
        boolean transaction2 = false;

        for (var field : callback.getStaticFields()) {
            if (field.getName().equals("TRANSACTION_onEvent")) {
                transaction2 = ((IntEncodedValue) field.getInitialValue()).getValue() == 2;
            }
        }

        check(transaction2, "targeted callback2");
        ClassDef event = type(api, "classes.dex", "Lcom/qinggan/system/QGKeyEvent;");
        check(
                parcelCalls(method(event, "writeToParcel"))
                        .equals(
                                List.of(
                                        "writeInt",
                                        "writeInt",
                                        "writeInt",
                                        "writeBundle",
                                        "writeInt")),
                "owner parcel writer");
        check(
                parcelCalls(method(event, "createFromParcelBody"))
                        .equals(List.of("readInt", "readInt", "readInt", "readBundle", "readInt")),
                "owner parcel reader");
        ClassDef camera =
                type(
                        launcher,
                        "classes2.dex",
                        "Lcom/qinggan/launcher/base/utils/CameraVuiHandler$3;");
        boolean photo = false, handler = false;

        for (var instruction :
                method(camera, "onKeyShortClick").getImplementation().getInstructions()) {
            if (instruction instanceof org.jf.dexlib2.iface.instruction.WideLiteralInstruction) {
                photo |=
                        ((org.jf.dexlib2.iface.instruction.WideLiteralInstruction) instruction)
                                        .getWideLiteral()
                                == 173;
            }

            if (instruction instanceof ReferenceInstruction) {
                handler |=
                        ((ReferenceInstruction) instruction)
                                .getReference()
                                .toString()
                                .contains("CameraVuiHandler;->access$300");
            }
        }

        check(photo && handler, "actual Free photo173 consumer");
        System.out.println(
                "Actual Free owner SHA/Binder/parcel/photo ABI PASS; Android runtime and visible"
                        + " effects unverified");
    }
}
