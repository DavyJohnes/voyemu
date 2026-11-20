package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;

public class PassionSystemExtReceiptTest {
    interface Action {
        void run();
    }

    static void reject(Action a) {
        try {
            a.run();
            throw new AssertionError("Wrong logical view accepted");
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }
    }

    public static void main(String[] args) throws Exception {
        Map<String, Object> base =
                Json.obj(
                        new ProfileRegistry(Path.of(args[0]).toRealPath())
                                .get(
                                        TestProfiles.declaring(
                                                Path.of(args[0]).toRealPath(),
                                                PassionSecondaryNavigationGuard.NAME))
                                .data(),
                        "android");
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
                        "system_ext",
                        "partition_offset",
                        793772032L,
                        "logical_partition_bytes",
                        106733568L,
                        "lp_metadata_sha256",
                        "f535d7f845ccdbd69a01d41639031e7d1a4950f21dc30df57dc4a438af030eee");
        String path = "/system_ext/priv-app/SystemUI/SystemUI.apk";

        PreparationMain.validateAndroidReceipt(base, path, receipt);

        for (int mode = 0; mode < 7; mode++) {
            var changed = new HashMap<>(receipt);

            if (mode == 0) {
                changed.put("logical_partition", "system");
            }

            if (mode == 1) {
                changed.put("partition_offset", 3145728L);
            }

            if (mode == 2) {
                changed.put("logical_partition_bytes", 789696512L);
            }

            if (mode == 3) {
                changed.put("lp_metadata_sha256", "0".repeat(64));
            }

            if (mode == 4) {
                changed.put(
                        "image_sha256",
                        "73d34da0e80bac7be61fee9eae610a3414e1e355b17796795a0d0869f9c1427b");
            }

            if (mode == 5) {
                changed.put("native", false);
            }

            if (mode == 6) {
                changed.put("filesystem_mounted", true);
            }

            reject(() -> PreparationMain.validateAndroidReceipt(base, path, changed));
        }

        reject(() ->
                PreparationMain.validateAndroidReceipt(
                        base, "/system/priv-app/SystemUI/SystemUI.apk", receipt));
        reject(() ->
                PreparationMain.validateAndroidReceipt(
                        base, "/system_ext2/priv-app/SystemUI/SystemUI.apk", receipt));
        var a9 = new HashMap<>(base);
        a9.put("package", "system-images;android-28;default;arm64-v8a");
        reject(() -> PreparationMain.validateAndroidReceipt(a9, path, receipt));
        var unpinned = new HashMap<>(base);
        unpinned.remove("clean_base");
        reject(() -> PreparationMain.validateAndroidReceipt(unpinned, path, receipt));
        System.out.println(
                "Exact SDK API30 system_ext receipt GREEN; eleven cross-view/base/path/pin"
                        + " negatives rejected");
    }
}
