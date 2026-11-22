package com.voyemu.prepare;

import org.jf.dexlib2.AccessFlags;
import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.reference.MethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** The pinned Drive recorder calls the virtual camera helper instead of the DVR display client. */
public final class PassionDvrVirtualGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static void check(boolean ok, String reason) {
        if (!ok) {
            throw new AssertionError(reason);
        }
    }

    static void deny(Action action, String reason) throws Exception {
        try {
            action.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError(reason);
    }

    static Map<String, Method> methods(byte[] dex, String owner) {
        var found = new HashMap<String, Method>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            if (c.getType().equals(owner)) {
                for (var m : c.getMethods()) {
                    found.put(PassionDvrVirtualGuard.key(m), m);
                }
            }
        }

        return found;
    }

    static List<String> calls(Method m) {
        var out = new ArrayList<String>();

        for (var i : m.getImplementation().getInstructions()) {
            if (i instanceof ReferenceInstruction r
                    && r.getReference() instanceof MethodReference ref) {
                out.add(PassionDvrVirtualGuard.text(ref));
            }
        }

        return out;
    }

    static int units(Method m) {
        int n = 0;

        for (var i : m.getImplementation().getInstructions()) {
            n += i.getCodeUnits();
        }

        return n;
    }

    public static void main(String[] args) throws Exception {
        Path apk = Path.of(args[0]);
        var entries = NativeCore.entries(apk);
        byte[] client = entries.get(PassionDvrVirtualGuard.CLIENT_DEX),
                preview = entries.get(PassionDvrVirtualGuard.PREVIEW_DEX);
        String helper = PassionDvrVirtualGuard.HELPER;

        // Original RED: the client is native and both loaders load the display client library.
        for (var m : methods(client, PassionDvrVirtualGuard.CLIENT).values()) {
            if (PassionDvrVirtualGuard.NATIVES.containsKey(PassionDvrVirtualGuard.key(m))) {
                check((m.getAccessFlags() & AccessFlags.NATIVE.getValue()) != 0, "native client");
            }
        }

        Method managerLoader =
                methods(client, PassionDvrVirtualGuard.MANAGER).get("loadPreviewLib()V");
        Method previewLoader =
                methods(preview, PassionDvrVirtualGuard.PREVIEW).get("loadPreviewLib()V");
        check(
                calls(managerLoader).contains(PassionDvrVirtualGuard.LOAD)
                        && calls(previewLoader).contains(PassionDvrVirtualGuard.LOAD),
                "original loaders load the display client");

        var result = PassionDvrVirtualGuard.apply(30, apk, client, preview);
        var natives = methods(result.get(0), PassionDvrVirtualGuard.CLIENT);
        check(
                calls(natives.get("startPreview(Ljava/lang/Object;)Z"))
                                .equals(
                                        List.of(
                                                helper
                                                        + "->startPreview(Ljava/lang/Object;Ljava/lang/Object;)Z"))
                        && calls(natives.get("stopPreview()Z"))
                                .equals(List.of(helper + "->stopPreview(Ljava/lang/Object;)Z"))
                        && calls(natives.get("takePicture()[B"))
                                .equals(List.of(helper + "->takePicture(Ljava/lang/Object;)[B")),
                "client methods call the camera helper with the client");

        for (var m : natives.values()) {
            check((m.getAccessFlags() & AccessFlags.NATIVE.getValue()) == 0, "client stays native");
        }

        check(
                calls(natives.get("onPreStatusChange(I)V"))
                        .equals(
                                calls(
                                        methods(client, PassionDvrVirtualGuard.CLIENT)
                                                .get("onPreStatusChange(I)V"))),
                "stream state callback kept");
        Method managerAfter =
                methods(result.get(0), PassionDvrVirtualGuard.MANAGER).get("loadPreviewLib()V");
        Method previewAfter =
                methods(result.get(1), PassionDvrVirtualGuard.PREVIEW).get("loadPreviewLib()V");

        for (var pair :
                List.of(
                        List.of(managerLoader, managerAfter),
                        List.of(previewLoader, previewAfter))) {
            check(
                    !calls(pair.get(1)).contains(PassionDvrVirtualGuard.LOAD)
                            && units(pair.get(1)) == units(pair.get(0)),
                    "loader keeps its code addresses without the library load");
        }

        int nops = 0;

        for (var i : previewAfter.getImplementation().getInstructions()) {
            nops += i.getOpcode() == Opcode.NOP ? 1 : 0;
        }

        check(nops == 5, "the const-string and invoke units become NOPs");
        check(
                methods(result.get(1), PassionDvrVirtualGuard.PREVIEW).size()
                        == methods(preview, PassionDvrVirtualGuard.PREVIEW).size(),
                "preview manager keeps its methods");
        deny(() -> PassionDvrVirtualGuard.apply(29, apk, client, preview), "API 29 accepted");
        deny(() -> PassionDvrVirtualGuard.apply(30, apk, preview, client), "swapped DEX accepted");
        deny(
                () -> PassionDvrVirtualGuard.apply(30, apk, result.get(0), result.get(1)),
                "rewritten client accepted again");
        deny(() -> PassionDvrVirtualGuard.validate(30, apk, client, null), "missing DEX accepted");

        // The helper DEX defines every static method the client calls.
        Path root = Path.of(args[1]), platform = Path.of(args[2]), work = Path.of(args[3]);
        Path source =
                root.resolve("profiles")
                        .resolve(TestProfiles.declaring(root, PassionDvrVirtualGuard.NAME))
                        .resolve("patches")
                        .resolve(PassionDvrVirtualGuard.HELPER_ROOT);
        Path dex = GuestJava.dex(new GuestJava.Unit(source, 30), platform, work);
        var defined = new ArrayList<String>();

        for (Path file : FilesSupport.tree(dex).values()) {
            for (var c :
                    new DexBackedDexFile(Opcodes.forApi(30), java.nio.file.Files.readAllBytes(file))
                            .getClasses()) {
                for (var m : c.getMethods()) {
                    if ((m.getAccessFlags() & AccessFlags.STATIC.getValue()) != 0
                            && (m.getAccessFlags() & AccessFlags.PUBLIC.getValue()) != 0) {
                        defined.add(
                                c.getType()
                                        + "->"
                                        + m.getName()
                                        + "("
                                        + String.join("", m.getParameterTypes())
                                        + ")"
                                        + m.getReturnType());
                    }
                }
            }
        }

        for (var m : natives.values()) {
            for (String call : calls(m)) {
                check(!call.startsWith(helper) || defined.contains(call), "helper lacks " + call);
            }
        }

        System.out.println(
                "Passion Drive recorder PASS: camera helper client, loaders without the display"
                        + " client, four negatives, helper contract");
    }
}
