package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/** Verifies actual C++ draft handoff; never invokes the artifact builder or a guest. */
public final class Api30NativeFileReceiptTest {
    private record Handoff(
            ProfileRegistry.Profile profile,
            Map<String, Object> request,
            Map<String, Object> sourcePins,
            Map<String, Path> android,
            PreparationMain.PinnedInput pinned) {}

    /**
     * Consumer-only handoff fixture for a reviewed inactive recipe; never edits canonical metadata
     * or runs a producer.
     */
    private static void hiboardDraft(Path root, Path retained, Path recipe) throws Exception {
        var request = Json.object(Json.parse(Files.readString(retained.resolve("request.json"))));

        if (!Api30NativeFiles.QINGGAN11_731.equals(Json.str(request, "profile"))) {
            throw new AssertionError("Exact Free A11 7.3.1 retained transport required");
        }

        String step = "property-contexts-api30.json",
                id = "property-contexts-api30",
                target = "/system/etc/selinux/plat_property_contexts";
        var draftRecipe = Json.object(Json.parse(Files.readString(recipe)));
        String recipePin = FilesSupport.hash(recipe);

        if (!Json.str(draftRecipe, "input_sha256")
                        .equals("a49213f01ea1f8ec19b41311116bc9324c682af1482297e4bbbc05265dfd623e")
                || !Json.str(draftRecipe, "output_sha256")
                        .equals(
                                "50c13ef43f195b1b94c5e1ed85f3a33578220e6074d9d244414ae73c9837d2db")) {
            throw new AssertionError("Exact reviewed Hiboard property recipe required");
        }

        var nativeReceipt =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        Path.of(
                                                Json.str(
                                                        Json.obj(request, "native_files"),
                                                        "receipt")))));
        var extraction = Json.obj(Json.obj(request, "android_inputs"), target);
        Path input =
                Path.of(Json.str(extraction, "receipt")).getParent().resolve(target.substring(1));

        if (!FilesSupport.hash(input).equals(Json.str(draftRecipe, "input_sha256"))) {
            throw new AssertionError("Original property input pin differs");
        }

        byte[] expected =
                (Files.readString(input)
                                + "persist.clusterCardInfo u:object_r:system_prop:s0 exact string\n"
                                + "ignoreActivation u:object_r:system_prop:s0 exact bool\n")
                        .getBytes(java.nio.charset.StandardCharsets.UTF_8);

        if (!NativeCore.hash(expected).equals(Json.str(draftRecipe, "output_sha256"))) {
            throw new AssertionError("Reviewed native property output bytes differ");
        }

        Path work =
                Files.createTempDirectory(
                        root.resolve(".local/build/tmp"), "hiboard-property-consumer-fixture-");

        try {
            Path originalReceipt = Path.of(Json.str(Json.obj(request, "native_files"), "receipt"));

            for (Object item : Json.list(nativeReceipt.get("files"))) {
                var entry = Json.object(item);
                String name = Json.str(entry, "output");

                if (Json.str(entry, "id").equals(id)) {
                    Files.write(work.resolve(name), expected);
                } else {
                    Files.copy(originalReceipt.getParent().resolve(name), work.resolve(name));
                }
            }

            var entries = Json.list(nativeReceipt.get("files"));
            var property = Json.object(entries.get(4));

            if (!id.equals(property.get("id"))) {
                throw new AssertionError("Exact existing native property slot required");
            }

            property.put("output_sha256", Json.str(draftRecipe, "output_sha256"));
            entries.set(4, property);
            nativeReceipt.put("files", entries);
            var provenance = Json.obj(nativeReceipt, "profile_provenance");
            var nodes = Json.obj(provenance, "nodes");
            var node = Json.object(nodes.get(id));
            node.put("output_sha256", Json.str(draftRecipe, "output_sha256"));
            nodes.put(id, node);
            provenance.put("nodes", nodes);
            nativeReceipt.put("profile_provenance", provenance);
            Path ownReceipt = work.resolve("receipt.json");
            Files.writeString(ownReceipt, Json.write(nativeReceipt));
            request.put(
                    "native_files",
                    Map.of(
                            "receipt",
                            ownReceipt.toString(),
                            "receipt_sha256",
                            FilesSupport.hash(ownReceipt)));
            var metadata =
                    Json.object(
                            Json.parse(Files.readString(Path.of(Json.str(request, "patch_data")))));
            draftRecipe.put("sourceSha256", recipePin);
            metadata.put(step, draftRecipe);
            Path ownMetadata = work.resolve("patch-data.json");
            Files.writeString(ownMetadata, Json.write(metadata));
            request.put("patch_data", ownMetadata.toString());
            request.put("patch_data_sha256", FilesSupport.hash(ownMetadata));
            Files.writeString(work.resolve("request.json"), Json.write(request));
            main(new String[] {root.toString(), work.toString()});
        } finally {
            FilesSupport.removeOwnedTree(work);
        }
    }

    private static void registryCompatibility(Path oldClasses) throws Exception {
        try (var loader =
                new java.net.URLClassLoader(
                        new java.net.URL[] {oldClasses.toUri().toURL()}, null)) {
            Class<?> old = loader.loadClass("com.voyemu.prepare.Api30NativeFiles");
            var select = old.getDeclaredMethod("forProfile", String.class);
            select.setAccessible(true);

            for (String profile : List.of(Api30NativeFiles.PASSION, "unscoped-api30")) {
                if (!select.invoke(null, profile)
                        .toString()
                        .equals(Api30NativeFiles.forProfile(profile).toString())) {
                    throw new AssertionError("Legacy Passion/default native registry rows changed");
                }
            }

            var oldFree = (List<?>) select.invoke(null, Api30NativeFiles.QINGGAN11_731);
            var current = Api30NativeFiles.forProfile(Api30NativeFiles.QINGGAN11_731);

            if (current.size() != 9) {
                throw new AssertionError("Exact 9-node Free native graph required");
            }

            var oldIds = new java.util.HashSet<String>();

            for (Object row : oldFree) {
                var id = row.getClass().getDeclaredMethod("id");
                id.setAccessible(true);
                oldIds.add((String) id.invoke(row));
            }

            var added = new java.util.TreeSet<String>();

            for (Object row : current) {
                var id = row.getClass().getDeclaredMethod("id");
                id.setAccessible(true);
                added.add((String) id.invoke(row));
            }

            added.removeAll(oldIds);

            if (!added.equals(
                    java.util.Set.of(
                            "original-navi-mac",
                            "can-simulator-contexts-api30",
                            "can-early-file-contexts-api30",
                            "can-simulator-policy-api30",
                            "can-early-policy-api30",
                            "can-early-property-contexts-api30"))) {
                throw new AssertionError(
                        "Only exact new can/original-navi Free nodes may be added: " + added);
            }

            var kept = new java.util.ArrayList<String>();

            for (Object row : current) {
                var id = row.getClass().getDeclaredMethod("id");
                id.setAccessible(true);
                kept.add((String) id.invoke(row));
            }

            var keptSet = new java.util.HashSet<>(kept);

            for (String legacyId :
                    new String[] {
                        "keymanager-contexts-api30", "public-qg-runtime-api30", "selinux-hash-api30"
                    }) {
                if (!keptSet.contains(legacyId)) {
                    throw new AssertionError("Legacy Free registry row dropped: " + legacyId);
                }
            }
        }

        System.out.println(
                "Legacy Passion/default registry rows preserved; only exact Free can/original-navi"
                        + " nodes added");
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 2 && args[0].equals("--registry-compatibility")) {
            registryCompatibility(Path.of(args[1]).toRealPath());

            return;
        }

        if (args.length == 4 && args[0].equals("--hiboard-draft")) {
            hiboardDraft(
                    Path.of(args[1]).toRealPath(),
                    Path.of(args[2]).toRealPath(),
                    Path.of(args[3]).toRealPath());

            return;
        }

        if (args.length != 2) {
            throw new IllegalArgumentException("ROOT ACTUAL_TRANSPORT");
        }

        Path root = Path.of(args[0]).toRealPath(), transport = Path.of(args[1]).toRealPath();
        var request = Json.object(Json.parse(Files.readString(transport.resolve("request.json"))));
        var profile =
                new ProfileRegistry(root, Json.str(request, "profile"))
                        .get(Json.str(request, "profile"));
        Map<String, Object> sourcePins = new TreeMap<>();
        var metadata =
                Json.object(Json.parse(Files.readString(Path.of(Json.str(request, "patch_data")))));

        for (var entry : metadata.entrySet()) {
            sourcePins.put(entry.getKey(), Json.str(Json.object(entry.getValue()), "sourceSha256"));
        }

        Map<String, Path> android = androidInputs(request);
        PreparationMain.PinnedInput pinned = pinnedFile();
        var outputs =
                PreparationMain.nativeFileOutputs(request, profile, sourcePins, android, pinned);

        if (outputs.size()
                != Json.list(
                                Json.object(
                                                Json.parse(
                                                        Files.readString(
                                                                Path.of(
                                                                        Json.str(
                                                                                Json.obj(
                                                                                        request,
                                                                                        "native_files"),
                                                                                "receipt")))))
                                        .get("files"))
                        .size()) {
            throw new AssertionError("Native handoff output missing");
        }

        Path original = outputs.values().iterator().next();
        String originalPin = FilesSupport.hash(original);
        Path receipt = Path.of(Json.str(Json.obj(request, "native_files"), "receipt"));
        String text = Files.readString(receipt);
        Path scratch =
                Files.createTempDirectory(
                        root.resolve(".local/build/tmp"), "api30-native-handoff-");
        int rejected = 0;
        var handoff = new Handoff(profile, request, sourcePins, android, pinned);

        try {
            for (int mode = 0; mode < malformedModes(profile, outputs); mode++) {
                checkMalformedDraft(mode, scratch, original, outputs, text, handoff);
                rejected++;
            }

            checkMissingReceipt(handoff);
            rejected++;
            rejected += checkApi30Graph(scratch, outputs, text, handoff);
        } finally {
            FilesSupport.removeOwnedTree(scratch);
        }

        if (!FilesSupport.hash(original).equals(originalPin)) {
            throw new AssertionError("Native output mutated by consumer");
        }

        System.out.println(
                Json.write(
                        Map.of(
                                "native_files",
                                outputs.size(),
                                "malformed_handoffs_rejected",
                                rejected,
                                "native_output_mutated",
                                false,
                                "guest_started",
                                false)));
    }

    private static int malformedModes(ProfileRegistry.Profile profile, Map<String, Path> outputs) {
        return (Api30NativeFiles.QINGGAN11_731.equals(profile.id())
                ? 32
                : Api30NativeFiles.PASSION.equals(profile.id())
                        ? 22
                        : outputs.size() == 4 ? 20 : 12);
    }

    private static int checkApi30Graph(
            Path scratch, Map<String, Path> outputs, String text, Handoff handoff)
            throws Exception {
        var profile = handoff.profile();
        int rejected = 0;

        if (Api30NativeFiles.QINGGAN11_731.equals(profile.id())) {
            for (boolean empty : List.of(false, true)) {
                checkPublicVisibilityRequired(empty, handoff);
                rejected++;
            }
        }

        if (Api30NativeFiles.QINGGAN11_731.equals(profile.id())) {
            checkKeyManagerContextRequired(handoff);
            rejected++;

            for (int mode = 0; mode < 2; mode++) {
                checkKeyManagerScope(mode, scratch, outputs, text, handoff);
                rejected++;
            }
        }

        if (Api30NativeFiles.QINGGAN11_731.equals(profile.id())) {
            checkPropertyContextRequired(handoff);
            rejected++;
        }

        return rejected;
    }

    private static Map<String, Path> androidInputs(Map<String, Object> request) {
        Map<String, Path> android = new TreeMap<>();

        for (var entry : Json.obj(request, "android_inputs").entrySet()) {
            Path receipt = Path.of(Json.str(Json.object(entry.getValue()), "receipt"));
            String logical = entry.getKey();
            android.put(
                    logical,
                    receipt.getParent()
                            .resolve(
                                    logical.startsWith("/vendor/")
                                            ? logical.substring(8)
                                            : logical.substring(1)));
        }

        return android;
    }

    private static PreparationMain.PinnedInput pinnedFile() {
        PreparationMain.PinnedInput pinned =
                (file, pin) -> {
                    try {
                        Failure.require(
                                Files.isRegularFile(file)
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

        return pinned;
    }

    private static void checkMalformedDraft(
            int mode,
            Path scratch,
            Path original,
            Map<String, Path> outputs,
            String text,
            Handoff handoff)
            throws Exception {
        var profile = handoff.profile();
        var request = handoff.request();
        var sourcePins = handoff.sourcePins();
        var android = handoff.android();
        var pinned = handoff.pinned();
        Path directory = scratch.resolve("case-" + mode);

        Files.createDirectories(directory);
        Path own = directory.resolve(original.getFileName());

        for (Path outputFile : outputs.values()) {
            Files.copy(outputFile, directory.resolve(outputFile.getFileName()));
        }

        var draft = Json.object(Json.parse(text));
        var provenance = Json.obj(draft, "profile_provenance");
        var files = new ArrayList<>(Json.list(draft.get("files")));
        var output = Json.object(files.get(0));

        switch (mode) {
            case 0 -> provenance.put("profile", "other-profile");
            case 1 -> provenance.put("native", false);
            case 2 ->
                    provenance.put(
                            "system_image_sha256",
                            "1d38dd537caa931441dc92949098b0f61b112587769877266f79f6e826822009");
            case 3 -> provenance.put("extraction_receipt_sha256", "0".repeat(64));
            case 4 -> provenance.put("machine_ready", true);
            case 5 -> provenance.put("nodes", Map.of());
            case 6 -> output.put("input", "/outside");
            case 7 -> output.put("api", 28L);
            case 8 -> output.put("id", "other");
            case 9 -> output.put("output", "../outside");
            case 10 -> Files.writeString(own, "corrupt context");
            case 11 -> draft.put("files", List.of());
            default -> {}
        }

        alterScopedNodes(mode, profile, files, output, provenance, android);
        draft.put("profile_provenance", provenance);

        if (mode != 11) {
            files.set(0, output);
            draft.put("files", files);
        }

        Path changed = directory.resolve("receipt.json");
        Files.writeString(changed, Json.write(draft));
        var selected = Json.object(Json.parse(Json.write(request)));
        selected.put(
                "native_files",
                Map.of(
                        "receipt",
                        changed.toString(),
                        "receipt_sha256",
                        FilesSupport.hash(changed)));
        boolean failed = false;

        try {
            PreparationMain.nativeFileOutputs(selected, profile, sourcePins, android, pinned);
        } catch (RuntimeException expected) {
            failed = true;
        }

        if (!failed) {
            throw new AssertionError("Malformed native handoff accepted mode" + mode);
        }
    }

    private static void alterScopedNodes(
            int mode,
            ProfileRegistry.Profile profile,
            List<Object> files,
            Map<String, Object> output,
            Map<String, Object> provenance,
            Map<String, Path> android) {
        if (mode == 20 || mode == 21) {
            var scoped =
                    Api30NativeFiles.QINGGAN11_731.equals(profile.id())
                            ? Json.object(files.get(3))
                            : output;

            if (mode == 20) {
                scoped.remove("profile");
            } else {
                scoped.put("profile", "other-profile");
            }

            if (Api30NativeFiles.QINGGAN11_731.equals(profile.id())) {
                files.set(3, scoped);
            }
        }

        if (mode == 22 || mode == 23) {
            var scoped = Json.object(files.get(3));

            if (mode == 22) {
                scoped.put("output_sha256", "0".repeat(64));
            } else {
                scoped.put("donor", Map.of("input", "/outside", "sha256", "0".repeat(64)));
            }

            files.set(3, scoped);
        }

        if (mode == 24) {
            files.remove(3);
        }

        if (mode == 25) {
            provenance.put("nodes", Map.of());
        }

        alterHashNode(mode, files, provenance, android);
        alterPropertyNode(mode, files);

        if (mode == 30) {
            files.remove(4);
        }

        if (mode == 31) {
            provenance.put("nodes", Map.of());
        }
    }

    private static void alterHashNode(
            int mode,
            List<Object> files,
            Map<String, Object> provenance,
            Map<String, Path> android) {
        if (mode >= 12 && mode <= 19) {
            int hashIndex = files.size() - 1;
            var hash = Json.object(files.get(hashIndex));

            if (mode == 12) {
                hash.remove("donor");
            }

            if (mode == 13) {
                hash.put(
                        "donor",
                        Map.of("input", "/outside", "sha256", Api30NativeFiles.MAPPING_PIN));
            }

            if (mode == 14) {
                hash.put(
                        "donor",
                        Map.of(
                                "input",
                                android.get(Api30NativeFiles.MAPPING).toString(),
                                "sha256",
                                "0".repeat(64)));
            }

            if (mode == 15) {
                hash.remove("patched_dependency");
            }

            if (mode == 16) {
                hash.put(
                        "patched_dependency",
                        Map.of("id", "selinux-services-api30", "sha256", "0".repeat(64)));
            }

            files.set(hashIndex, hash);

            if (mode == 17) {
                files.set(1, files.get(0));
            }

            if (mode == 18) {
                files.add(files.get(0));
            }

            if (mode == 19) {
                provenance.put("nodes", Map.of());
            }
        }
    }

    private static void alterPropertyNode(int mode, List<Object> files) {
        if (mode >= 26 && mode <= 29) {
            var scoped = Json.object(files.get(4));

            if (mode == 26) {
                scoped.remove("profile");
            }

            if (mode == 27) {
                scoped.put("profile", "other-profile");
            }

            if (mode == 28) {
                scoped.put("output_sha256", "0".repeat(64));
            }

            if (mode == 29) {
                scoped.put("donor", Map.of("input", "/outside", "sha256", "0".repeat(64)));
            }

            files.set(4, scoped);
        }
    }

    private static void checkMissingReceipt(Handoff handoff) throws Exception {
        var profile = handoff.profile();
        var request = handoff.request();
        var sourcePins = handoff.sourcePins();
        var android = handoff.android();
        var pinned = handoff.pinned();
        var missing = new TreeMap<>(request);

        missing.remove("native_files");
        boolean failed = false;

        try {
            PreparationMain.nativeFileOutputs(missing, profile, sourcePins, android, pinned);
        } catch (Failure expected) {
            failed = true;
        }

        if (!failed) {
            throw new AssertionError("Missing native receipt accepted");
        }
    }

    private static void checkPublicVisibilityRequired(boolean empty, Handoff handoff)
            throws Exception {
        var profile = handoff.profile();
        var request = handoff.request();
        var sourcePins = handoff.sourcePins();
        var android = handoff.android();
        var pinned = handoff.pinned();
        var changed = Json.object(profile.data());
        var patchNodes = Json.obj(changed, "patches");

        if (empty) {
            patchNodes.clear();
        } else {
            patchNodes.remove("public-qg-runtime-api30");
        }

        changed.put("patches", patchNodes);
        var incomplete =
                new ProfileRegistry.Profile(
                        profile.id(),
                        profile.revision(),
                        changed,
                        profile.resourcePrefix(),
                        profile.sourceRoot());
        var selected = new TreeMap<>(request);

        if (empty) {
            selected.remove("native_files");
        }

        boolean denied = false;

        try {
            PreparationMain.nativeFileOutputs(selected, incomplete, sourcePins, android, pinned);
        } catch (Failure expected) {
            denied = expected.code.equals("PREPARATION_NATIVE_FILE");
        }

        if (!denied) {
            throw new AssertionError(
                    "Current Free A11 7.3.1 requires public visibility even with"
                            + " missing/all-removed native graph");
        }
    }

    private static void checkKeyManagerContextRequired(Handoff handoff) throws Exception {
        var profile = handoff.profile();
        var request = handoff.request();
        var sourcePins = handoff.sourcePins();
        var android = handoff.android();
        var pinned = handoff.pinned();
        var changed = Json.object(profile.data());
        var nodes = Json.obj(changed, "patches");

        nodes.remove("keymanager-contexts-api30");
        changed.put("patches", nodes);
        var incomplete =
                new ProfileRegistry.Profile(
                        profile.id(),
                        profile.revision(),
                        changed,
                        profile.resourcePrefix(),
                        profile.sourceRoot());
        boolean denied = false;

        try {
            PreparationMain.nativeFileOutputs(request, incomplete, sourcePins, android, pinned);
        } catch (Failure expected) {
            denied = expected.code.equals("PREPARATION_NATIVE_FILE");
        }

        if (!denied) {
            throw new AssertionError("OEM wheel requires exact scoped KeyManager context");
        }
    }

    private static void checkKeyManagerScope(
            int mode, Path scratch, Map<String, Path> outputs, String text, Handoff handoff)
            throws Exception {
        var profile = handoff.profile();
        var request = handoff.request();
        var sourcePins = handoff.sourcePins();
        var android = handoff.android();
        var pinned = handoff.pinned();
        Path directory = scratch.resolve("keymanager-context-" + mode);

        Files.createDirectories(directory);

        for (Path outputFile : outputs.values()) {
            Files.copy(outputFile, directory.resolve(outputFile.getFileName()));
        }

        var draft = Json.object(Json.parse(text));
        var files = new ArrayList<>(Json.list(draft.get("files")));
        var scoped = Json.object(files.get(2));

        if (mode == 0) {
            scoped.remove("profile");
        } else {
            scoped.put("profile", "other-profile");
        }

        files.set(2, scoped);
        draft.put("files", files);
        Path altered = directory.resolve("receipt.json");
        Files.writeString(altered, Json.write(draft));
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
            PreparationMain.nativeFileOutputs(selected, profile, sourcePins, android, pinned);
        } catch (Failure expected) {
            denied = true;
        }

        if (!denied) {
            throw new AssertionError("KeyManager context scope mismatch accepted");
        }
    }

    private static void checkPropertyContextRequired(Handoff handoff) throws Exception {
        var profile = handoff.profile();
        var request = handoff.request();
        var sourcePins = handoff.sourcePins();
        var android = handoff.android();
        var pinned = handoff.pinned();
        var changed = Json.object(profile.data());
        var nodes = Json.obj(changed, "patches");

        nodes.remove("property-contexts-api30");
        changed.put("patches", nodes);
        var incomplete =
                new ProfileRegistry.Profile(
                        profile.id(),
                        profile.revision(),
                        changed,
                        profile.resourcePrefix(),
                        profile.sourceRoot());
        boolean denied = false;

        try {
            PreparationMain.nativeFileOutputs(request, incomplete, sourcePins, android, pinned);
        } catch (Failure expected) {
            denied = expected.code.equals("PREPARATION_NATIVE_FILE");
        }

        if (!denied) {
            throw new AssertionError("Current Free A11 7.3.1 requires cluster property context");
        }
    }
}
