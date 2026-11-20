package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Set;
import java.util.zip.ZipFile;

public final class PassionMcuPropertyGuardTest {
    static void rejects(Throwing action) throws Exception {
        try {
            action.run();
        } catch (IllegalArgumentException | Failure expected) {
            return;
        }

        throw new AssertionError("guard accepted invalid contract");
    }

    interface Throwing {
        void run() throws Exception;
    }

    static void checkManifest(Path apk, Path out) throws Exception {
        byte[] manifest;

        try (var zip = new ZipFile(apk.toFile())) {
            manifest = zip.getInputStream(zip.getEntry("AndroidManifest.xml")).readAllBytes();
        }

        byte[] finalManifest =
                PassionMcuManifest.add(
                        ManifestLibraries.add(
                                manifest,
                                List.of(
                                        "android.car",
                                        "voyemu.platform.callbacks",
                                        "voyemu.window",
                                        "voyemu.mega")));
        Files.write(out.resolve("AndroidManifest-after-framework.xml"), finalManifest);
        byte[] patchedManifest = PassionMcuManifest.add(manifest);
        Files.write(out.resolve("AndroidManifest.xml"), patchedManifest);
        rejects(() -> PassionMcuManifest.add(patchedManifest));
        byte[] foreignOwner = manifest.clone();
        byte[] from = "android.uid.system".getBytes(java.nio.charset.StandardCharsets.UTF_16LE),
                to = "android.uid.otherx".getBytes(java.nio.charset.StandardCharsets.UTF_16LE);
        int changes = 0;

        for (int n = 0; n <= foreignOwner.length - from.length; n++) {
            if (java.util.Arrays.equals(
                    java.util.Arrays.copyOfRange(foreignOwner, n, n + from.length), from)) {
                System.arraycopy(to, 0, foreignOwner, n, to.length);
                changes++;
            }
        }

        if (changes != 1) {
            throw new AssertionError("UID fixture missing");
        }

        rejects(() -> PassionMcuManifest.add(foreignOwner));
    }

    static void checkHelperReceipt(Path apk, Path out, byte[] helper) throws Exception {
        Path prepared = out.resolve("receipt.apk");

        try (var zip = new java.util.zip.ZipOutputStream(Files.newOutputStream(prepared))) {
            zip.putNextEntry(new java.util.zip.ZipEntry("classes18.dex"));
            zip.write(helper);
            zip.closeEntry();
        }

        var receipt = PassionMcuHelperContract.receipt(prepared);

        if (!receipt.get("sha256").equals(NativeCore.hash(helper))
                || !receipt.get("archive_sha256").equals(FilesSupport.hash(prepared))
                || !receipt.get("bytes").equals((long) helper.length)
                || !receipt.get("entry").equals("classes18.dex")) {
            throw new AssertionError("actual MCU output receipt differs");
        }

        var outputs =
                PreparationMain.artifactOutputs(
                        java.util.Map.of(PassionMcuHelperContract.ARTIFACT, prepared),
                        false,
                        Set.of(),
                        true);

        if (!Json.obj(Json.obj(outputs, PassionMcuHelperContract.ARTIFACT), "mcu_helper")
                .equals(receipt)) {
            throw new AssertionError("artifact output lost MCU helper receipt");
        }

        rejects(() -> PreparationMain.artifactOutputs(java.util.Map.of(), false, Set.of(), true));
        rejects(() -> PassionMcuHelperContract.receipt(apk));
    }

    public static void main(String[] args) throws Exception {
        Path apk = Path.of(args[0]), out = Path.of(args[1]);

        Files.createDirectories(out);
        Path normal =
                PassionMcuPropertyGuard.compileHelper(
                        TestProfiles.SYNTHETIC,
                        30,
                        apk,
                        Path.of(args[2]),
                        Path.of(args[3]),
                        out.resolve("normal-helper"));
        Files.createDirectories(out.resolve("helper-dex"));
        Files.copy(normal, out.resolve("helper-dex/classes.dex"));
        var selected = new ArrayList<ClassDef>();
        Set<String> names =
                Set.of(
                        "Lcom/mega/car/hal/IProperty;",
                        "Lcom/mega/car/hal/IProperty$Receiver;",
                        "Lmega/car/hardware/CarPropertyConfig;",
                        "Lmega/car/hardware/CarPropertyValue;",
                        "Lmega/car/hardware/property/CarPropertyEvent;");
        byte[] factory = null;

        try (var zip = new ZipFile(apk.toFile())) {
            for (var entry : Collections.list(zip.entries())) {
                if (entry.getName().matches("classes[0-9]*\\.dex")) {
                    byte[] data = zip.getInputStream(entry).readAllBytes();

                    if (entry.getName().equals("classes16.dex")) {
                        factory = data;
                    }

                    for (var c : new DexBackedDexFile(Opcodes.forApi(30), data).getClasses()) {
                        if (names.contains(c.getType())) {
                            selected.add(c);
                        }
                    }
                }
            }
        }

        if (selected.size() != names.size()) {
            throw new AssertionError("missing actual OEM signatures");
        }

        Signatures.jar(selected, out.resolve("actual-oem-signatures.jar"));
        checkManifest(apk, out);
        byte[] helper = Files.readAllBytes(out.resolve("helper-dex/classes.dex"));
        PassionMcuPropertyGuard.validateHelper(helper);

        checkHelperReceipt(apk, out, helper);

        final byte[] dex = factory;
        PassionMcuPropertyGuard.validate(TestProfiles.SYNTHETIC, 30, apk, dex);
        byte[] patched = PassionMcuPropertyGuard.apply(TestProfiles.SYNTHETIC, 30, apk, dex);
        Files.write(out.resolve("patched-classes16.dex"), patched);
        rejects(() -> PassionMcuPropertyGuard.apply(TestProfiles.SYNTHETIC, 29, apk, dex));
        rejects(() -> PassionMcuPropertyGuard.apply(TestProfiles.SYNTHETIC, 30, apk, patched));
        System.out.println(
                "PASS exact original hook, all-class inverse, profile/API/tamper/already-patched"
                        + " guards; signatures="
                        + selected.size()
                        + " patched="
                        + NativeCore.hash(patched));
    }
}
