package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** One exact API28 codec output; no transform and no replacement of legacy Java file nodes. */
final class Api28CodecNativeFile {
    static final String ID = "ccodec-allocation-a9",
            TARGET = "/system/lib64/libstagefright_ccodec.so";
    static final String INPUT = "2262964922a876a51c3b5186f094afb136ce9daae0678f9820f11e86e058e353",
            OUTPUT = "11d1e2cb76995985aa319e08fca8a6286bc7c701ecb71ed5a7822b4816bce86f";

    static String output(ProfileRegistry.Profile profile) {
        return OUTPUT;
    }

    static Map<String, Object> node() {
        return Map.of("type", "file", "input", "android:" + TARGET, "steps", List.of(ID + ".json"));
    }

    static boolean selected(ProfileRegistry.Profile profile) {
        return Json.obj(profile.data(), "patches").containsKey(ID);
    }

    static boolean accepts(ProfileRegistry.Profile profile, int api, String reference, Path file)
            throws Exception {
        return api == 28
                && reference.equals("patched:" + ID)
                && node().equals(Json.obj(profile.data(), "patches").get(ID))
                && Files.isRegularFile(file)
                && !Files.isSymbolicLink(file)
                && FilesSupport.hash(file).equals(output(profile));
    }

    private static void need(boolean value, String message) {
        Failure.require(value, "PREPARATION_NATIVE_FILE", message, 3);
    }

    static Map<String, Path> outputs(
            Map<String, Object> request,
            ProfileRegistry.Profile profile,
            Map<String, Object> sourcePins,
            Map<String, Path> android,
            PreparationMain.PinnedInput pinned)
            throws Exception {
        var base = Json.obj(profile.data(), "android");

        need(
                "system-images;android-28;default;arm64-v8a".equals(base.get("package")),
                "Exact A9 codec base/profile required");
        need(
                node().equals(Json.obj(profile.data(), "patches").get(ID))
                        && sourcePins.containsKey(ID + ".json"),
                "Exact codec node/source required");
        long installed = 0;

        for (Object item : Json.list(profile.data().get("install"))) {
            var entry = Json.object(item);

            if (("patched:" + ID).equals(entry.get("artifact"))) {
                need(
                        entry.equals(
                                Map.of(
                                        "artifact",
                                        "patched:" + ID,
                                        "type",
                                        "file",
                                        "target",
                                        TARGET,
                                        "mode",
                                        "0644")),
                        "Exact codec install required");
                installed++;
            }
        }

        need(installed == 1, "One codec install required");
        need(request.containsKey("native_files"), "Native API28 codec receipt missing");
        var handoff = Json.obj(request, "native_files");
        Path receiptFile = Path.of(Json.str(handoff, "receipt"));
        Path root = Path.of(Json.str(request, "root"));
        need(
                root.isAbsolute()
                        && root.normalize().equals(root)
                        && receiptFile.isAbsolute()
                        && receiptFile.normalize().equals(receiptFile)
                        && receiptFile.startsWith(root.resolve(".local")),
                "Confined codec receipt required");
        pinned.verify(receiptFile, Json.str(handoff, "receipt_sha256"));
        need(Files.size(receiptFile) <= 128 * 1024, "Bounded codec receipt required");
        var receipt = Json.object(Json.parse(Files.readString(receiptFile)));
        need(
                Json.integer(receipt, "schema") == 1
                        && "native-file-draft".equals(receipt.get("kind"))
                        && Json.list(receipt.get("files")).size() == 1,
                "One native codec output required");
        var extraction = Json.object(Json.obj(request, "android_inputs").get(TARGET));
        Path extractionFile = Path.of(Json.str(extraction, "receipt"));
        String extractionPin = Json.str(extraction, "receipt_sha256");
        pinned.verify(extractionFile, extractionPin);
        var extractionReceipt = Json.object(Json.parse(Files.readString(extractionFile)));
        PreparationMain.validateAndroidReceipt(base, TARGET, extractionReceipt);
        Path original = android.get(TARGET);
        need(
                original != null
                        && original.equals(extractionFile.getParent().resolve(TARGET.substring(1))),
                "Codec extraction path binding differs");
        pinned.verify(original, INPUT);
        need(Files.size(original) == 632488, "Exact codec input size required");
        var inputEntry = Json.object(Json.obj(extractionReceipt, "files").get(TARGET.substring(1)));
        need(
                INPUT.equals(inputEntry.get("sha256"))
                        && Json.integer(inputEntry, "bytes") == 632488,
                "Codec extraction entry differs");
        var expected = new LinkedHashMap<String, Object>();
        expected.put("schema", 1L);
        expected.put("profile", profile.id());
        expected.put("native", true);
        expected.put("native_scope", "api28-codec-only");
        expected.put("machine_ready", false);
        expected.put(
                "system_image_sha256", PreparationMain.cleanBasePin(base, "images", "system.img"));
        expected.put("extraction_receipt_sha256", extractionPin);
        expected.put(
                "nodes",
                Map.of(
                        ID,
                        Map.of(
                                "input_sha256",
                                INPUT,
                                "output_sha256",
                                output(profile),
                                "target",
                                TARGET)));
        need(
                Json.obj(receipt, "profile_provenance").equals(expected),
                "Codec profile/source/extraction provenance differs");
        var output = Json.object(Json.list(receipt.get("files")).get(0));
        var expectedOutput = new LinkedHashMap<String, Object>();
        expectedOutput.put("id", ID);
        expectedOutput.put("api", 28L);
        expectedOutput.put("input", original.toString());
        expectedOutput.put("input_sha256", INPUT);
        expectedOutput.put("output", ID + ".output");
        expectedOutput.put("output_sha256", output(profile));
        expectedOutput.put("profile", profile.referenceProfile());
        need(output.equals(expectedOutput), "Codec output scope differs");
        Path file = receiptFile.getParent().resolve(ID + ".output");
        pinned.verify(file, output(profile));
        need(Files.size(file) == 632488, "Exact codec output size required");

        return Map.of("patched:" + ID, file);
    }

    private Api28CodecNativeFile() {}
}
