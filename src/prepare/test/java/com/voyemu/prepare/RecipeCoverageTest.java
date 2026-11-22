package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.TreeSet;

/**
 * Registration/nonempty-data coverage only; actual ordered OEM transforms require artifact
 * acceptance.
 */
public final class RecipeCoverageTest {
    public static void main(String[] args) throws Exception {
        Path project =
                Path.of(
                                args.length > 0
                                        ? args[0]
                                        : System.getProperty(
                                                "voyemu.project.root",
                                                System.getProperty("user.dir")))
                        .toRealPath();
        Path profile = project.resolve("profiles/free-09-chinese-20260106-6.0.3");
        Map<String, Object> definition =
                Json.object(Json.parse(Files.readString(profile.resolve("profile.json"))));
        Map<String, Object> metadata =
                ProfileRegistry.patchMetadata(
                        new ProfileRegistry(project).get("free-09-chinese-20260106-6.0.3"));
        var steps = new TreeSet<String>();

        for (Object value : Json.obj(definition, "patches").values()) {
            for (Object step : Json.list(Json.object(value).get("steps"))) {
                steps.add((String) step);
            }
        }

        for (String step : steps) {
            Map<String, Object> data = Json.object(metadata.get(step));
            checkStep(profile, definition, step, data);
        }

        checkMediaContract(profile, metadata);
        System.out.println(
                "all "
                        + steps.size()
                        + " source-bound recipes covered by native or Java"
                        + " operations; ordered OEM acceptance remains separate");
    }

    static void checkStep(
            Path profile, Map<String, Object> definition, String step, Map<String, Object> data)
            throws Exception {
        if (!FilesSupport.hash(Storage.confined(profile.resolve("patches"), step))
                .equals(Json.str(data, "sourceSha256"))) {
            throw new AssertionError("metadata source revision differs: " + step);
        }

        if (step.equals(Api28CodecNativeFile.ID + ".json")) {
            checkNativeCodec(definition, data);

            return;
        }

        if (step.equals("phone-provider.json")) {
            checkPhoneProvider(profile, definition, step, data);
        }

        for (Object value : Json.list(data.get("calls"))) {
            var call = Json.object(value);
            String operation = Json.str(call, "operation");
            boolean galleryGuard =
                    step.equals("api28-gallery-arm64.json")
                            && operation.equals("guardGalleryArm64")
                            && Json.list(call.get("args")).isEmpty();

            if (!galleryGuard
                    && !java.util.Set.of(
                                    "editLines",
                                    "editMethod",
                                    "addDex",
                                    "removeEntry",
                                    "useLibraries")
                            .contains(operation)) {
                throw new AssertionError("Embedded original replacement is forbidden: " + step);
            }
        }

        if (!Recipes.hasRegisteredCoverage(step, data)) {
            throw new AssertionError(
                    "recipe has neither explicit handler nor nonempty captured calls: " + step);
        }
    }

    static void checkNativeCodec(Map<String, Object> definition, Map<String, Object> data) {
        var nativeData = Json.obj(data, "native");

        if (Json.integer(data, "api") != 28
                || data.containsKey("calls")
                || !Api28CodecNativeFile.node()
                        .equals(Json.obj(definition, "patches").get(Api28CodecNativeFile.ID))
                || !Api28CodecNativeFile.INPUT.equals(Json.str(nativeData, "input_sha256"))
                || !Api28CodecNativeFile.OUTPUT.equals(Json.str(nativeData, "output_sha256"))) {
            throw new AssertionError("exact native codec registration differs");
        }
    }

    static void checkPhoneProvider(
            Path profile, Map<String, Object> definition, String step, Map<String, Object> data)
            throws Exception {
        if (!Json.list(Json.obj(Json.obj(definition, "patches"), "BluetoothPhone").get("steps"))
                        .contains(step)
                || !Json.list(data.get("calls")).isEmpty()
                || !Json.obj(data, "constants").isEmpty()) {
            throw new AssertionError("exact own-code phone provider recipe differs");
        }

        PhoneRecipeTest.main(new String[] {profile.resolve("patches").resolve(step).toString()});
    }

    static void checkMediaContract(Path profile, Map<String, Object> metadata) {
        Map<String, Object> mediaContract =
                Json.obj(Json.object(metadata.get("media-runtime.json")), "contract");

        if (Json.integer(mediaContract, "schema") != 1
                || Json.str(mediaContract, "original_sha256").length() != 64
                || Json.list(mediaContract.get("shared_libraries")).isEmpty()) {
            throw new AssertionError("self-contained media contract lost during profile loading");
        }

        if (Files.exists(profile.resolve("patches/media-runtime/contract.json"))) {
            throw new AssertionError("obsolete media contract sidecar remains");
        }

        if (Recipes.hasRegisteredCoverage(
                "ivoka-silent-foreground.json",
                Map.of(
                        "sourceSha256",
                        Json.str(
                                Json.object(metadata.get("ivoka-silent-foreground.json")),
                                "sourceSha256"),
                        "calls",
                        java.util.List.of()))) {
            throw new AssertionError("empty silent recipe falsely covered");
        }
    }
}
