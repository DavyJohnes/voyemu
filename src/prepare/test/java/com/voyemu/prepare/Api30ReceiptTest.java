package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;

public final class Api30ReceiptTest {
    public static void main(String[] args) throws Exception {
        var root = java.nio.file.Path.of(System.getProperty("user.dir")).toRealPath();
        String selected = "free-11-chinese-20250710-6.11.1";

        checkRegistryVisibility(root, selected);
        Map<String, Object> base =
                Json.obj(new ProfileRegistry(root).get(selected).data(), "android");
        Map<String, Object> receipt =
                Map.of(
                        "schema",
                        1L,
                        "native",
                        true,
                        "filesystem_mounted",
                        false,
                        "image_sha256",
                        "b9879a686e47e26d1ba1c2f2eafd417e5f9415d68a4e03a0c2adae47f3fb53ec",
                        "logical_partition",
                        "system",
                        "partition_offset",
                        3145728L,
                        "logical_partition_bytes",
                        789696512L,
                        "lp_metadata_sha256",
                        "f535d7f845ccdbd69a01d41639031e7d1a4950f21dc30df57dc4a438af030eee");
        PreparationMain.validateAndroidReceipt(base, "/system/own", receipt);

        for (int mode = 0; mode < 7; mode++) {
            var b = new HashMap<>(base);
            var r = new HashMap<>(receipt);

            if (mode == 0) {
                r.put(
                        "image_sha256",
                        "1d38dd537caa931441dc92949098b0f61b112587769877266f79f6e826822009");
            }

            if (mode == 1) {
                r.remove("logical_partition");
            }

            if (mode == 2) {
                r.put("lp_metadata_sha256", "0".repeat(64));
            }

            if (mode == 3) {
                r.put("partition_offset", 0L);
            }

            if (mode == 4) {
                r.put("logical_partition_bytes", 789696513L);
            }

            if (mode == 5) {
                b.put("package", "system-images;android-29;default;arm64-v8a");
            }

            if (mode == 6) {
                b.remove("clean_base");
            }

            boolean failed = false;

            try {
                PreparationMain.validateAndroidReceipt(b, "/system/own", r);
            } catch (Failure expected) {
                failed = true;
            }

            if (!failed) {
                throw new AssertionError("Wrong base or logical extraction accepted");
            }
        }

        System.out.println(
                "API30 exact base/raw-image/LP receipt guards passed without helper launch or"
                        + " guest");
    }

    private static void checkRegistryVisibility(java.nio.file.Path root, String selected)
            throws Exception {
        var released = new ProfileRegistry(root).get(selected);
        var prepared = new ProfileRegistry(root, selected).get(selected);

        if (!"ready".equals(released.data().get("status"))
                || !released.data().equals(prepared.data())) {
            throw new AssertionError("Ready profile visibility or source status changed");
        }

        Path fixture =
                Files.createTempDirectory(root.resolve(".local/build/tmp"), "api30-registry-");

        try {
            for (String id : new String[] {"ready", "selected", "other"}) {
                Path directory = fixture.resolve("profiles/" + id);
                Files.createDirectories(directory);
                Files.writeString(
                        directory.resolve("profile.json"),
                        Json.write(
                                Map.of(
                                        "schema",
                                        1L,
                                        "status",
                                        id.equals("ready") ? "ready" : "incomplete")));
            }

            var defaults = new ProfileRegistry(fixture);
            defaults.get("ready");
            requireHidden(defaults, "selected");
            requireHidden(defaults, "other");
            var preparation = new ProfileRegistry(fixture, "selected");

            if (!"incomplete".equals(preparation.get("selected").data().get("status"))) {
                throw new AssertionError("Host preparation changed source status");
            }

            requireHidden(preparation, "other");
            requireHidden(new ProfileRegistry(fixture, "other"), "selected");
        } finally {
            FilesSupport.removeOwnedTree(fixture);
        }
    }

    private static void requireHidden(ProfileRegistry registry, String id) {
        boolean hidden = false;

        try {
            registry.get(id);
        } catch (Failure expected) {
            hidden = true;
        }

        if (!hidden) {
            throw new AssertionError("Registry exposed unselected incomplete profile: " + id);
        }
    }
}
