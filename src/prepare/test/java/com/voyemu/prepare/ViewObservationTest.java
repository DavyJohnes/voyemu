package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.LinkedHashMap;

public final class ViewObservationTest {
    static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]).toRealPath(),
                original = Path.of(args[1]),
                helper = Path.of(args[2]),
                output = Path.of(args[3]);
        String id = "free-11-chinese-20250710-6.11.1";
        var data =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve("profiles/" + id + "/profile.json"))));
        var profile =
                new ProfileRegistry.Profile(id, "test", data, "voyemu/profiles/" + id + "/", root);
        var component =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve(
                                                "profiles/"
                                                        + id
                                                        + "/components/view-observation/component.json"))));

        check(Api30ViewObservation.supports(id));
        check(!Api30ViewObservation.supports(id + "-other"));
        check(
                Api30ViewObservation.source(profile, component)
                        .equals(root.resolve(Json.str(component, "api30ViewObservation"))));
        var invalid = new LinkedHashMap<>(component);
        invalid.put("api30ViewObservation", "profiles/foreign/src");

        try {
            Api30ViewObservation.source(profile, invalid);
            throw new AssertionError("foreign source accepted");
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }

        try {
            Api30ViewObservation.build(id, 28, original, helper, output);
            throw new AssertionError("wrong API accepted");
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }

        try {
            Api30ViewObservation.build(id + "-other", 30, original, helper, output);
            throw new AssertionError("wrong profile accepted");
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }

        Api30ViewObservation.build(id, 30, original, helper, output);
        var before = NativeCore.entries(original);
        var after = NativeCore.entries(output);
        check(before.keySet().equals(after.keySet()));

        for (String name : before.keySet()) {
            if (!name.equals("classes3.dex")) {
                check(Arrays.equals(before.get(name), after.get(name)));
            }
        }

        var donor =
                FrameworkBridgeVerifier.jumboHelper(NativeCore.entries(helper).get("classes.dex"));
        FrameworkBridgeVerifier.verify(
                before.get("classes3.dex"), donor, after.get("classes3.dex"));
        System.out.println(
                "Free A11 6.11.1 view bridge preserves other entries and rejects unsupported"
                        + " inputs; no guest acceptance");
    }
}
