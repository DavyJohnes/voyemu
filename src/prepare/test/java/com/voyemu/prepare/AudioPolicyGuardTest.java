package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

public final class AudioPolicyGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static int negatives = 0;

    static void check(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    static void reject(Action a) throws Exception {
        try {
            a.run();
        } catch (Failure expected) {
            negatives++;

            return;
        }

        throw new AssertionError("accepted wrong contract");
    }

    static String reference(Instruction i) {
        return i instanceof ReferenceInstruction r ? r.getReference().toString() : "";
    }

    public static void main(String[] arguments) throws Exception {
        long start = System.nanoTime();
        // The first argument names the profile for the runner; the guard reads only step data.
        String[] args = Arrays.copyOfRange(arguments, 1, arguments.length);
        Path apk = Path.of(args[0]), stage = Path.of(args[1]);
        Map<String, Object> step =
                args.length == 5
                        ? Json.obj(
                                Json.object(Json.parse(Files.readString(Path.of(args[4])))),
                                "constants")
                        : Map.of(StepConstants.PINS, List.of(FilesSupport.hash(apk)));
        byte[] dex;

        try (var z = new ZipFile(apk.toFile())) {
            dex = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }

        byte[] after = AudioPolicyGuard.apply(step, 30, "arm64-v8a", apk, dex);
        var original = AudioPolicyGuard.classes(dex);
        var actual = AudioPolicyGuard.classes(after);
        int delegates = 0, boxed = 0;
        Set<String> sourceInterface = new TreeSet<>(), delegated = new TreeSet<>();

        // A QGAPI JAR carries the AudioPolicy interface; otherwise its AIDL Smali is given.
        if (args[3].endsWith(".jar")) {
            Path qgapi = Path.of(args[3]);
            ClassDef iface = null;

            for (var type : DexOperations.dex(qgapi, "classes.dex", 30).getClasses()) {
                if (type.getType().equals("Lcom/qinggan/audiopolicy/IAudioPolicyService;")) {
                    iface = type;
                }
            }

            check(iface != null);

            for (var method : iface.getMethods()) {
                if ((method.getAccessFlags() & 0x401) == 0x401) {
                    sourceInterface.add(AudioPolicyGuard.signature(method));
                }
            }
        } else {
            for (String line : Files.readAllLines(Path.of(args[3]))) {
                if (line.startsWith(".method public abstract ")) {
                    sourceInterface.add(line.substring(line.lastIndexOf(' ') + 1));
                }
            }
        }

        for (var m : actual.get(AudioPolicyGuard.STUB).getMethods()) {
            if ((m.getAccessFlags() & 1) != 0) {
                delegates++;
                delegated.add(AudioPolicyGuard.signature(m));
                boxed += checkDelegateBody(m);
            }
        }

        check(delegates == 116 && sourceInterface.equals(delegated));
        checkPreservedOwners(original, actual);
        checkRejectedApplies(args, apk, dex, step);
        check(Arrays.equals(after, AudioPolicyGuard.apply(step, 30, "arm64-v8a", apk, dex)));
        Files.write(stage.resolve("guarded-classes.dex"), after);
        var afterPins = new TreeMap<String, String>();

        for (var type : actual.values()) {
            afterPins.put(type.getType(), FrameworkDexMaterializer.semantic(type));
        }

        Files.writeString(
                stage.resolve("transformed-class-pins.json"), Json.write(afterPins) + "\n");
        checkManifestProof(args, apk, stage, after);
        check(StepConstants.admits(step, FilesSupport.hash(apk)));
        var receipt = new TreeMap<String, Object>();
        receipt.put("delegatedInterfaceMethods", delegates);
        receipt.put("boxedArguments", boxed);
        receipt.put("negativeChecks", negatives);
        receipt.put("allClassInverse", true);
        receipt.put("deterministicApply", true);
        receipt.put("originalConstructorReceiverAndAidlPreserved", true);
        receipt.put("outputSha256", NativeCore.hash(after));
        receipt.put("elapsedSeconds", (System.nanoTime() - start) / 1e9);
        receipt.put("requiredSharedLibrary", "QGAPI");
        receipt.put("originalApplicationSuperclassPreserved", true);
        receipt.put("binaryManifestProof", "manifest-proof.apk");
        receipt.put(
                "boundary",
                "Host DEX and unsigned binary-manifest proof only; production signing, helper"
                        + " runtime, ART verifier and VM/device untested");
        Files.writeString(stage.resolve("receipt.json"), Json.write(receipt) + "\n");
        System.out.println(Json.write(receipt));
    }

    static int checkDelegateBody(Method m) {
        int boxed = 0;
        int invokes = 0, argsCount = 0, unboxes = 0;
        var b = m.getImplementation();

        check(b.getRegisterCount() == 7 + m.getParameterTypes().size());

        for (var i : b.getInstructions()) {
            if (reference(i).contains("->dispatch(")) {
                invokes++;
            }

            if (i.getOpcode() == Opcode.APUT_OBJECT) {
                argsCount++;
            }

            if (reference(i).contains("->valueOf(")) {
                boxed++;
            }

            if (reference(i).contains("->intValue()")
                    || reference(i).contains("->booleanValue()")) {
                unboxes++;
            }

            if (i instanceof FiveRegisterInstruction r) {
                int[] regs = {
                    r.getRegisterC(),
                    r.getRegisterD(),
                    r.getRegisterE(),
                    r.getRegisterF(),
                    r.getRegisterG()
                };

                for (int j = 0; j < r.getRegisterCount(); j++) {
                    check(regs[j] < 16 && regs[j] < b.getRegisterCount());
                }
            }

            check(!reference(i).contains("Lbc;") && !reference(i).contains("AudioPolicyNative"));
        }

        check(invokes == 1 && argsCount == m.getParameterTypes().size());
        check(
                unboxes
                        == ((m.getReturnType().equals("I") || m.getReturnType().equals("Z"))
                                ? 1
                                : 0));

        return boxed;
    }

    static void checkPreservedOwners(Map<String, ClassDef> original, Map<String, ClassDef> actual)
            throws Exception {
        for (String owner :
                List.of(
                        "Lcom/qinggan/audiopolicy/IAudioPolicyService;",
                        "Lcom/qinggan/audiopolicy/IAudioPolicyService$Stub;",
                        "Lcom/qinggan/audiopolicy/IAudioPolicyService$Stub$Proxy;",
                        AudioPolicyGuard.SERVICE.replace(";", "$2;"))) {
            check(original.containsKey(owner) == actual.containsKey(owner));

            if (original.containsKey(owner)) {
                check(
                        FrameworkDexMaterializer.semantic(original.get(owner))
                                .equals(FrameworkDexMaterializer.semantic(actual.get(owner))));
            }
        }

        for (var m : actual.get(AudioPolicyGuard.SERVICE).getMethods()) {
            if (m.getName().equals("onCreate")) {
                int calls = 0;

                for (var i : m.getImplementation().getInstructions()) {
                    check(
                            !reference(i).contains("Lbc;")
                                    && !reference(i).contains("Lbj;")
                                    && !reference(i).contains("AudioPolicyNative"));

                    if (i.getOpcode().name.startsWith("invoke")) {
                        calls++;
                    }
                }

                check(calls == 4);
            }
        }
    }

    static void checkRejectedApplies(String[] args, Path apk, byte[] dex, Map<String, Object> step)
            throws Exception {
        reject(() ->
                AudioPolicyGuard.apply(
                        Map.of(StepConstants.PINS, List.of("0".repeat(64))),
                        30,
                        "arm64-v8a",
                        apk,
                        dex));
        reject(() -> AudioPolicyGuard.apply(step, 29, "arm64-v8a", apk, dex));
        reject(() -> AudioPolicyGuard.apply(step, 30, "x86_64", apk, dex));
        reject(() -> AudioPolicyGuard.apply(step, 30, "arm64", apk, dex));
        reject(() -> AudioPolicyGuard.apply(step, 30, "arm64-v8a", Path.of(args[2]), dex));
    }

    static void checkManifestProof(String[] args, Path apk, Path stage, byte[] after)
            throws Exception {
        byte[] manifest;

        try (var zip = new ZipFile(apk.toFile())) {
            manifest = zip.getInputStream(zip.getEntry("AndroidManifest.xml")).readAllBytes();
        }

        if (args.length < 4 || args.length > 5) {
            throw new IllegalArgumentException(
                    "PROFILE ORIGINAL_APK STAGE WRONG_APK QGAPI|AIDL_SMALI [RECIPE]");
        }

        if (args.length == 5) {
            var recipe = Json.object(Json.parse(Files.readString(Path.of(args[4]))));
            check(
                    Json.list(recipe.get("calls"))
                            .equals(
                                    List.of(
                                            Map.of(
                                                    "operation",
                                                    "guardAudioPolicy",
                                                    "args",
                                                    List.of()),
                                            Map.of(
                                                    "operation",
                                                    "useLibraries",
                                                    "args",
                                                    List.of(List.of("QGAPI"))))));
        }

        byte[] patchedManifest = ManifestLibraries.add(manifest, List.of("QGAPI"));
        check(!Arrays.equals(manifest, patchedManifest));
        reject(() -> ManifestLibraries.add(patchedManifest, List.of("QGAPI")));
        Files.write(stage.resolve("AndroidManifest.xml"), patchedManifest);
        check(
                AudioPolicyGuard.classes(after)
                        .get("Lcom/qinggan/audiopolicy/service/OSAudioPolicyApplication;")
                        .getSuperclass()
                        .equals("Lcom/qinggan/app/QGApplication;"));

        try (var zip = new ZipFile(apk.toFile());
                var output =
                        new ZipOutputStream(
                                Files.newOutputStream(stage.resolve("manifest-proof.apk")))) {
            for (var entries = zip.entries(); entries.hasMoreElements(); ) {
                var entry = entries.nextElement();
                output.putNextEntry(new ZipEntry(entry.getName()));
                output.write(
                        entry.getName().equals("AndroidManifest.xml")
                                ? patchedManifest
                                : entry.getName().equals("classes.dex")
                                        ? after
                                        : zip.getInputStream(entry).readAllBytes());
                output.closeEntry();
            }
        }
    }
}
