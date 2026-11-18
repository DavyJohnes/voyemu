package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;

/**
 * Source draft and actual owner APK through the ordinary atomic input importer; no guest or APK
 * preparation.
 */
public final class HiboardInputsTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1",
            PIN = "902562587355d9e48d3878773da7955cd5a793c03e5ba6e5b595a317c7ec0f39";

    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            throw new IllegalArgumentException("ROOT INTEGRATION_DIRECTORY ORIGINAL_HIBOARD_APK");
        }

        Path root = Path.of(args[0]).toRealPath(),
                draftRoot = Path.of(args[1]).toRealPath(),
                apk = Path.of(args[2]).toRealPath();
        var before =
                Json.object(Json.parse(Files.readString(draftRoot.resolve("profile.before.json"))));
        var draft =
                Json.object(Json.parse(Files.readString(draftRoot.resolve("profile.draft.json"))));
        check(
                FilesSupport.hash(root.resolve("profiles/" + PROFILE + "/profile.json"))
                        .equals(FilesSupport.hash(draftRoot.resolve("profile.before.json"))),
                "Canonical profile must remain unchanged during draft verification");
        var definition = Json.obj(draft, "original").get("Hiboard");
        check(
                definition.equals(
                        Map.of(
                                "file",
                                "Hiboard.apk",
                                "sha256",
                                PIN,
                                "source",
                                Map.of(
                                        "path",
                                        "/vendor/app/Hiboard-release-signed/Hiboard-release-signed.apk"))),
                "Exact original vendor APK pin/path required");
        check(
                Json.obj(draft, "patches")
                        .get("Hiboard")
                        .equals(
                                Map.of(
                                        "input",
                                        "original:Hiboard",
                                        "type",
                                        "apk",
                                        "steps",
                                        List.of(
                                                "api30-hiboard-libraries.json",
                                                "api30-hiboard-routing.json"))),
                "Only original framework dependency and actual routing required");
        var install =
                Map.of(
                        "artifact",
                        "patched:Hiboard",
                        "type",
                        "apk",
                        "target",
                        "/system/priv-app/Hiboard-release-signed/Hiboard-release-signed.apk",
                        "package",
                        "com.qinggan.app.hiboard");
        var installations = Json.list(draft.get("install"));
        check(
                installations.stream().filter(install::equals).count() == 1,
                "Exactly one genuine privileged Hiboard slot required");
        check(
                Json.obj(draft, "files").get("nvram").equals(Map.of("file", "nvram-active.txt")),
                "Draft must bind its own active emulator fixture");
        var ownSeed = root.resolve("profiles/" + PROFILE + "/files/nvram-active.txt");
        check(
                Files.readString(ownSeed)
                        .equals(
                                Files.readString(
                                                root.resolve(
                                                        "profiles/" + PROFILE + "/files/nvram.txt"))
                                        + "8=true\n"),
                "Exact active fixture delta with canonical seed preserved");
        var remainingFiles = Json.obj(draft, "files");
        remainingFiles.put("nvram", Json.obj(before, "files").get("nvram"));
        draft.put("files", remainingFiles);
        var remainingOriginals = Json.obj(draft, "original");
        remainingOriginals.remove("Hiboard");
        draft.put("original", remainingOriginals);
        var remainingPatches = Json.obj(draft, "patches");
        remainingPatches.remove("Hiboard");
        draft.put("patches", remainingPatches);
        installations.remove(install);
        draft.put("install", installations);
        check(
                draft.equals(before),
                "Hiboard graph and explicit own fixture must be the only draft delta");
        var full =
                Json.object(Json.parse(Files.readString(draftRoot.resolve("profile.draft.json"))));
        var fullProfile = new ProfileRegistry.Profile(PROFILE, "draft", full, "unused");
        Path data = root.resolve(".local");
        var local = new Storage(data, data, data, data, data, data);
        var missing = Inputs.check(fullProfile, local);
        check(
                !missing.ok()
                        && missing.checks().stream()
                                .filter(x -> "fail".equals(Json.object(x).get("status")))
                                .map(x -> Json.object(x).get("id"))
                                .toList()
                                .equals(List.of("Hiboard")),
                "RED: exact new Hiboard APK alone must be missing in the ordinary complete input"
                        + " check");
        check(
                Files.size(apk) == 26132593 && FilesSupport.hash(apk).equals(PIN),
                "Exact retained OEM input required");
        Path work =
                Files.createTempDirectory(
                        root.resolve(".local/build/tmp"), "qinggan11-731-hiboard-inputs-");

        try {
            checkOrdinaryImport(root, work, apk, definition);
        } finally {
            FilesSupport.removeOwnedTree(work);
        }

        check(
                !Files.exists(
                        root.resolve(".local/profiles/" + PROFILE + "/originals/Hiboard.apk")),
                "Canonical original must stay inactive");
        System.out.println(
                "Hiboard-only draft delta; full missing APK RED; actual OEM atomic import GREEN;"
                        + " wrong pin/idempotence PASS; canonical profile/inputs unchanged");
    }

    static void checkOrdinaryImport(Path root, Path work, Path apk, Object definition)
            throws Exception {
        Path owner = work.resolve("owner");

        Files.createDirectory(owner);
        Files.copy(apk, owner.resolve("Hiboard.apk"));
        var profile =
                new ProfileRegistry.Profile(
                        PROFILE,
                        "draft",
                        Map.of("original", Map.of("Hiboard", definition)),
                        "unused");
        var storage = Storage.resolve(work.resolve("home").toString(), Map.of(), "mac", root);
        check(!Inputs.check(profile, storage).ok(), "Fresh missing original must fail");
        var fetch = Arguments.parse(new String[] {"fetch", PROFILE, "--input", owner.toString()});
        var imported = Inputs.fetch(profile, storage, fetch, null, message -> {});
        check(
                Boolean.TRUE.equals(imported.get("changed")) && Inputs.check(profile, storage).ok(),
                "GREEN: ordinary atomic fetch and pin check must accept actual OEM APK");
        check(
                Inputs.receiptPins(profile, storage).equals(Map.of("Hiboard", PIN)),
                "Importer must generate its own exact receipt");
        check(
                Boolean.FALSE.equals(
                        Inputs.fetch(profile, storage, fetch, null, message -> {}).get("changed")),
                "Verified input import must be idempotent");
        Files.writeString(owner.resolve("Hiboard.apk"), "wrong OEM input");
        var other = Storage.resolve(work.resolve("rejected").toString(), Map.of(), "mac", root);
        boolean rejected = false;

        try {
            Inputs.fetch(profile, other, fetch, null, message -> {});
        } catch (Failure e) {
            rejected = e.code.equals("ORIGINAL_HASH_MISMATCH");
        }

        check(
                rejected && !Files.exists(other.originals(PROFILE)),
                "Wrong pin must fail before publication");
        check(
                FilesSupport.hash(apk).equals(PIN) && Inputs.check(profile, storage).ok(),
                "Original and prior accepted import must survive negative test");
    }
}
