package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.TreeMap;

/** Actual native ext4 -> planned draft -> Java artifact consumer; no Android guest. */
public final class Api28CodecNativeFileReceiptTest {
    private record Handoff(
            ProfileRegistry.Profile profile,
            Map<String, Object> request,
            Map<String, Object> pins,
            Map<String, Path> android,
            PreparationMain.PinnedInput pinned) {}

    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            throw new IllegalArgumentException("ROOT ACTUAL_NATIVE_DRAFT ACTUAL_EXTRACTION");
        }

        Path root = Path.of(args[0]).toRealPath(),
                draft = Path.of(args[1]).toRealPath(),
                extraction = Path.of(args[2]).toRealPath();
        String id = TestProfiles.declaring(root, Api28CodecNativeFile.ID);
        var profile = new ProfileRegistry(root, id).get(id);
        Path original = extraction.resolve("system/lib64/libstagefright_ccodec.so"),
                receipt = draft.resolve("receipt.json"),
                output = draft.resolve(Api28CodecNativeFile.ID + ".output");
        String originalPin = FilesSupport.hash(original),
                outputPin = FilesSupport.hash(output),
                receiptText = Files.readString(receipt);
        Map<String, Object> pins =
                Map.of(
                        Api28CodecNativeFile.ID + ".json",
                        FilesSupport.hash(
                                root.resolve("profiles")
                                        .resolve(profile.id())
                                        .resolve("patches")
                                        .resolve(Api28CodecNativeFile.ID + ".json")));
        Map<String, Path> android = Map.of(Api28CodecNativeFile.TARGET, original);
        Map<String, Object> request = new TreeMap<>();
        request.put("root", root.toString());
        request.put("profile", profile.id());
        request.put(
                "profile_sha256",
                FilesSupport.hash(
                        root.resolve("profiles").resolve(profile.id()).resolve("profile.json")));
        request.put(
                "native_files",
                Map.of(
                        "receipt",
                        receipt.toString(),
                        "receipt_sha256",
                        FilesSupport.hash(receipt)));
        request.put(
                "android_inputs",
                Map.of(
                        Api28CodecNativeFile.TARGET,
                        Map.of(
                                "receipt",
                                extraction.resolve("receipt.json").toString(),
                                "receipt_sha256",
                                FilesSupport.hash(extraction.resolve("receipt.json")))));
        PreparationMain.PinnedInput pinned =
                (file, pin) -> {
                    try {
                        Failure.require(
                                file.isAbsolute()
                                        && file.toRealPath().equals(file)
                                        && Files.isRegularFile(file)
                                        && !Files.isSymbolicLink(file)
                                        && FilesSupport.hash(file).equals(pin),
                                "TEST_PIN",
                                "Pinned file differs",
                                3);

                        return file;
                    } catch (RuntimeException e) {
                        throw e;
                    } catch (Exception e) {
                        throw new RuntimeException(e);
                    }
                };
        var outputs = PreparationMain.nativeFileOutputs(request, profile, pins, android, pinned);

        if (!outputs.equals(Map.of("patched:" + Api28CodecNativeFile.ID, output))) {
            throw new AssertionError(
                    "Must consume only exact codec output, not old eleven Java transforms");
        }

        Path scratch =
                Files.createTempDirectory(
                        root.resolve(".local/build/tmp"), "api28-codec-consumer-");
        int rejected = 0;
        var handoff = new Handoff(profile, request, pins, android, pinned);

        try {
            checkIndependentProfile(scratch, output, receiptText, handoff);
            checkArtifactResolver(root, scratch, output, outputs, profile);
            rejected++;

            for (int mode = 0; mode < 15; mode++) {
                checkAlteredReceipt(mode, scratch, output, receiptText, handoff);
                rejected++;
            }

            for (int mode = 0; mode < 3; mode++) {
                checkMissingBinding(mode, output, handoff);
                rejected++;
            }
        } finally {
            FilesSupport.removeOwnedTree(scratch);
        }

        if (!FilesSupport.hash(original).equals(originalPin)
                || !FilesSupport.hash(output).equals(outputPin)
                || !Files.readString(receipt).equals(receiptText)) {
            throw new AssertionError("Consumer mutated native input/output/receipt");
        }

        System.out.println(
                Json.write(
                        Map.of(
                                "native_files_consumed",
                                1,
                                "legacy_Java_nodes_replaced",
                                0,
                                "malformed_handoffs_rejected",
                                rejected,
                                "actual_ext4_input_pin",
                                originalPin,
                                "native_output_pin",
                                outputPin,
                                "guest_started",
                                false)));
    }

    private static void checkIndependentProfile(
            Path scratch, Path output, String receiptText, Handoff handoff) throws Exception {
        var profile = handoff.profile();
        var request = handoff.request();
        var pins = handoff.pins();
        var android = handoff.android();
        var pinned = handoff.pinned();
        var localData = new LinkedHashMap<String, Object>(profile.data());

        localData.put("id", "independent-local-copy");
        localData.put(
                "capture",
                Map.of("profile", "independent-local-copy", "referenceProfile", profile.id()));
        var localProfile =
                new ProfileRegistry.Profile(
                        "independent-local-copy",
                        profile.revision(),
                        localData,
                        profile.resourcePrefix());
        Path local = scratch.resolve("independent-codec");
        Files.createDirectories(local);
        Files.copy(output, local.resolve(output.getFileName()));
        var localReceipt = Json.object(Json.parse(receiptText));
        var localProvenance = Json.obj(localReceipt, "profile_provenance");
        localProvenance.put("profile", localProfile.id());
        localReceipt.put("profile_provenance", localProvenance);
        Path localReceiptFile = local.resolve("receipt.json");
        Files.writeString(localReceiptFile, Json.write(localReceipt));
        var localRequest = new TreeMap<>(request);
        localRequest.put("profile", localProfile.id());
        localRequest.put(
                "native_files",
                Map.of(
                        "receipt",
                        localReceiptFile.toString(),
                        "receipt_sha256",
                        FilesSupport.hash(localReceiptFile)));

        if (!PreparationMain.nativeFileOutputs(localRequest, localProfile, pins, android, pinned)
                .equals(
                        Map.of(
                                "patched:" + Api28CodecNativeFile.ID,
                                local.resolve(output.getFileName())))) {
            throw new AssertionError("Independent source profile lost native codec preparation");
        }
    }

    private static void checkArtifactResolver(
            Path root,
            Path scratch,
            Path output,
            Map<String, Path> outputs,
            ProfileRegistry.Profile profile)
            throws Exception {
        var builder =
                new Artifacts(
                        new Artifacts.PatchedProfile(profile, Map.of()),
                        null,
                        null,
                        root.resolve("profiles").resolve(profile.id()),
                        scratch.resolve("work"),
                        logical -> {
                            throw new AssertionError("Native codec consumer reread guest");
                        },
                        message -> {});

        builder.useNativeFile(
                "patched:" + Api28CodecNativeFile.ID,
                outputs.get("patched:" + Api28CodecNativeFile.ID));

        if (!builder.resolve("patched:" + Api28CodecNativeFile.ID).equals(output)) {
            throw new AssertionError("Artifact resolver did not use native output");
        }

        boolean duplicate = false;

        try {
            builder.useNativeFile("patched:" + Api28CodecNativeFile.ID, output);
        } catch (Failure expected) {
            duplicate = true;
        }

        if (!duplicate) {
            throw new AssertionError("Duplicate native codec accepted");
        }
    }

    private static void checkAlteredReceipt(
            int mode, Path scratch, Path output, String receiptText, Handoff handoff)
            throws Exception {
        var profile = handoff.profile();
        var request = handoff.request();
        var pins = handoff.pins();
        var android = handoff.android();
        var pinned = handoff.pinned();
        Path dir = scratch.resolve("case-" + mode);

        Files.createDirectories(dir);
        Path own = dir.resolve(output.getFileName());
        Files.copy(output, own);
        var changed = Json.object(Json.parse(receiptText));
        var provenance = Json.obj(changed, "profile_provenance");
        var files = new ArrayList<>(Json.list(changed.get("files")));
        var entry = Json.object(files.get(0));

        switch (mode) {
            case 0 -> provenance.put("profile", "other");
            case 1 -> provenance.put("native", false);
            case 2 -> provenance.put("system_image_sha256", "0".repeat(64));
            case 3 -> provenance.put("extraction_receipt_sha256", "0".repeat(64));
            case 4 -> provenance.put("machine_ready", true);
            case 5 -> provenance.put("nodes", Map.of());
            case 6 -> provenance.put("native_scope", "all-files");
            case 7 -> entry.put("api", 30L);
            case 8 -> entry.put("profile", "other");
            case 9 -> entry.put("output", "../outside");
            case 10 -> entry.put("input", "/outside");
            case 11 -> entry.put("output_sha256", "0".repeat(64));
            case 12 -> entry.put("donor", Map.of("input", "/outside", "sha256", "0".repeat(64)));
            case 13 -> Files.writeString(own, "corrupt codec");
            default -> {}
        }

        files.set(0, entry);

        if (mode == 14) {
            files.add(entry);
        }

        changed.put("files", files);
        changed.put("profile_provenance", provenance);
        Path altered = dir.resolve("receipt.json");
        Files.writeString(altered, Json.write(changed));
        var selected = new TreeMap<>(request);
        selected.put(
                "native_files",
                Map.of(
                        "receipt",
                        altered.toString(),
                        "receipt_sha256",
                        FilesSupport.hash(altered)));
        boolean denied = false;

        try {
            PreparationMain.nativeFileOutputs(selected, profile, pins, android, pinned);
        } catch (Failure expected) {
            denied = true;
        }

        if (!denied) {
            throw new AssertionError("Altered codec handoff accepted: " + mode);
        }
    }

    private static void checkMissingBinding(int mode, Path output, Handoff handoff)
            throws Exception {
        var profile = handoff.profile();
        var request = handoff.request();
        var pins = handoff.pins();
        var android = handoff.android();
        var pinned = handoff.pinned();
        var selected = new TreeMap<>(request);
        var selectedPins = new TreeMap<>(pins);
        var selectedAndroid = new TreeMap<>(android);

        if (mode == 0) {
            selected.remove("native_files");
        }

        if (mode == 1) {
            selectedPins.put(Api28CodecNativeFile.ID + ".json", "0".repeat(64));
        }

        if (mode == 2) {
            selectedAndroid.put(Api28CodecNativeFile.TARGET, output);
        }

        boolean denied = false;

        try {
            PreparationMain.nativeFileOutputs(
                    selected, profile, selectedPins, selectedAndroid, pinned);
        } catch (Failure expected) {
            denied = true;
        }

        if (!denied) {
            throw new AssertionError("Missing receipt/source/input binding accepted");
        }
    }
}
