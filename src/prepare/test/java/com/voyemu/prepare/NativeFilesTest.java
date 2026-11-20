package com.voyemu.prepare;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public final class NativeFilesTest {
    private static void require(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) throws Exception {
        var graph611 = Api30NativeFiles.forPlatform("qinggan11", false);
        var graph731 = Api30NativeFiles.forPlatform("qinggan11", true);

        require(
                graph611.size() == 8 && graph731.size() == 9,
                "Exact Free native graph sizes required");
        require(
                graph611.equals(
                        graph731.stream()
                                .filter(spec -> !spec.id().equals("original-navi-mac"))
                                .toList()),
                "qinggan11 exact non-Navi graph differs");
        require(
                graph611.stream().noneMatch(spec -> spec.id().equals("original-navi-mac")),
                "qinggan11 without original Navi must not select Navi MAC");
        require(
                Api30NativeFiles.nodeSteps("can-early-policy-api30")
                        .equals(
                                List.of(
                                        "selinux-services-api30.json",
                                        "can-simulator-policy-api30.json",
                                        "can-early-policy-api30.json")),
                "Early policy step order differs");
        require(
                Api30NativeFiles.nodeSteps("can-early-property-contexts-api30")
                        .equals(
                                List.of(
                                        "property-contexts-api30.json",
                                        "can-early-property-contexts-api30.json")),
                "Early property step order differs");
        boolean denied;

        for (int count : List.of(0, 1, 7)) {
            var patches = new LinkedHashMap<String, Object>();

            for (int i = 0; i < count; ++i) {
                patches.put(graph611.get(i).id(), Map.of("type", "file"));
            }

            var data =
                    Map.<String, Object>of(
                            "patches", patches, "platform", "qinggan11", "board", "8155");
            var profile = new ProfileRegistry.Profile(TestProfiles.SYNTHETIC, "test", data, "");
            denied = false;

            try {
                PreparationMain.nativeFileOutputs(
                        Map.of(), profile, Map.of(), Map.of(), (path, pin) -> path);
            } catch (Failure expected) {
                denied = "PREPARATION_NATIVE_FILE".equals(expected.code);
            }

            require(denied, "qinggan11 accepted incomplete native graph of size " + count);
        }

        require(
                !Api30NativeFiles.forPlatform("qinggan09", false).equals(graph611),
                "Another platform selected the qinggan11 graph");
        System.out.println("qinggan11 native graph and negative selection checks passed");
    }
}
