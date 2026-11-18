package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/** Real helper compilation and ordinary artifact-result receipt regressions. */
public final class PassionPowerHelperContractTest {
    interface Run {
        void run() throws Exception;
    }

    private static void check(boolean ok, String message) {
        if (!ok) {
            throw new AssertionError(message);
        }
    }

    private static void reject(Run run) throws Exception {
        try {
            run.run();
            throw new AssertionError("invalid helper receipt accepted");
        } catch (Failure expected) {
            check(expected.code.equals("PASSION_POWER_HELPER"), "unexpected rejection");
        }
    }

    private static byte[] missingHook(byte[] dex, boolean sdk) throws Exception {
        var classes = new ArrayList<ClassDef>();
        String requiredClass = sdk ? "DmsPowerCommand;" : "OemDisplayPower;";

        for (var type : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            if (!type.getType().endsWith(requiredClass)) {
                classes.add(type);
                continue;
            }

            var direct = new ArrayList<Method>();

            for (var method : type.getDirectMethods()) {
                if (!method.getName().equals(sdk ? "exec" : "send")) {
                    direct.add(method);
                }
            }

            classes.add(
                    new ImmutableClassDef(
                            type.getType(),
                            type.getAccessFlags(),
                            type.getSuperclass(),
                            type.getInterfaces(),
                            type.getSourceFile(),
                            type.getAnnotations(),
                            type.getStaticFields(),
                            type.getInstanceFields(),
                            direct,
                            type.getVirtualMethods()));
        }

        return FrameworkDexMaterializer.serialize(
                new ImmutableDexFile(Opcodes.forApi(30), classes));
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]).toRealPath(), platform = Path.of(args[1]).toRealPath();
        Path work = Path.of(args[2]);

        Files.createDirectories(work.getParent());
        Files.createDirectory(work);
        Path patches = root.resolve("profiles/" + PassionDisplayPowerGuard.PROFILE + "/patches");
        Map<String, Object> absent =
                Map.of(
                        "passion-api30-secondary-navigation", Map.of("steps", List.of()),
                        "MegaNexusService", Map.of("steps", List.of()));
        Map<String, Object> partial =
                Map.of(
                        "passion-api30-secondary-navigation",
                                Map.of("steps", List.of("passion-api30-display-power.json")),
                        "MegaNexusService", Map.of("steps", List.of()));
        Map<String, Object> declared =
                Map.of(
                        "passion-api30-secondary-navigation",
                                Map.of("steps", List.of("passion-api30-display-power.json")),
                        "MegaNexusService",
                                Map.of("steps", List.of("passion-api30-nexus-power.json")));
        check(
                !PassionPowerHelperContract.requested(PassionDisplayPowerGuard.PROFILE, absent),
                "no-power graph changed");
        reject(() ->
                PassionPowerHelperContract.requested(PassionDisplayPowerGuard.PROFILE, partial));
        check(
                !PassionPowerHelperContract.requested("unrelated", Map.of()),
                "unrelated profile changed");
        check(
                PassionPowerHelperContract.requested(PassionDisplayPowerGuard.PROFILE, declared),
                "declared graph missed");

        for (String artifact :
                List.of(PassionPowerHelperContract.SDK, PassionPowerHelperContract.OEM)) {
            boolean sdk = artifact.equals(PassionPowerHelperContract.SDK);
            String helper = sdk ? "passion-api30-display-power" : "passion-api30-nexus-power";
            Path dexDir =
                    DexOperations.javaDex(
                            patches.resolve(helper + "/src"),
                            null,
                            List.of(),
                            platform,
                            work.resolve(helper),
                            30);
            byte[] dex = Files.readAllBytes(dexDir.resolve("classes.dex"));
            PassionPowerHelperContract.verify(artifact, dex);
            reject(() -> PassionPowerHelperContract.verify(artifact, missingHook(dex, sdk)));
            reject(() -> PassionPowerHelperContract.verify(artifact, null));
            reject(() -> PassionPowerHelperContract.verify("unrelated", dex));
            Path archive = work.resolve(helper + ".jar");
            NativeCore.zip(archive, Map.of(PassionPowerHelperContract.slot(artifact), dex), false);
            var receipt = PassionPowerHelperContract.receipt(artifact, archive);
            check(
                    receipt.get("archive_sha256").equals(FilesSupport.hash(archive)),
                    "archive binding");
            check(receipt.get("sha256").equals(NativeCore.hash(dex)), "actual helper digest");
            check(receipt.get("bytes").equals((long) dex.length), "actual helper extent");
            var outputs = PreparationMain.artifactOutputs(Map.of(artifact, archive), true);
            check(
                    Json.obj(outputs, artifact).get("power_helper").equals(receipt),
                    "receipt missing from result");
            var unrelated = PreparationMain.artifactOutputs(Map.of(artifact, archive), false);
            check(
                    Json.obj(unrelated, artifact)
                            .keySet()
                            .equals(java.util.Set.of("file", "sha256", "bytes")),
                    "unrelated profile output expanded");
            var unrelatedArtifact =
                    PreparationMain.artifactOutputs(Map.of("patched:unrelated", archive), true);
            check(
                    Json.obj(unrelatedArtifact, "patched:unrelated").size() == 3,
                    "unrelated artifact expanded");
            Path wrong = work.resolve(helper + "-wrong-slot.jar");
            NativeCore.zip(wrong, Map.of("classes.dex", dex), false);
            check(
                    Json.obj(
                                            PreparationMain.artifactOutputs(
                                                    Map.of(artifact, wrong), false),
                                            artifact)
                                    .size()
                            == 3,
                    "no-power graph required an absent helper");
            reject(() -> PassionPowerHelperContract.receipt(artifact, wrong));
        }

        System.out.println(
                "PASS actual helper DEX ABI, computed archive receipts, missing-hook/slot and"
                        + " unrelated-output negatives");
    }
}
