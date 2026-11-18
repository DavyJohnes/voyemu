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
        var graph611 = Api30NativeFiles.forProfile(Api30NativeFiles.QINGGAN11_611, false);
        var graph731 = Api30NativeFiles.forProfile(Api30NativeFiles.QINGGAN11_731, true);

        require(
                graph611.size() == 8 && graph731.size() == 9,
                "Exact Free native graph sizes required");
        require(
                graph611.equals(
                        graph731.stream()
                                .filter(spec -> !spec.id().equals("original-navi-mac"))
                                .toList()),
                "Free A11 6.11.1 exact non-Navi graph differs");
        require(
                graph611.stream().noneMatch(spec -> spec.id().equals("original-navi-mac")),
                "Free A11 6.11.1 must not select Navi MAC");
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
        boolean denied = false;

        try {
            Api30NativeFiles.forProfile(Api30NativeFiles.QINGGAN11_611, true);
        } catch (IllegalStateException expected) {
            denied = true;
        }

        require(denied, "Free A11 6.11.1 accepted Navi MAC");
        denied = false;

        try {
            Api30NativeFiles.forProfile(Api30NativeFiles.QINGGAN11_731, false);
        } catch (IllegalStateException expected) {
            denied = true;
        }

        require(denied, "Free A11 7.3.1 accepted missing Navi MAC");

        for (int count : List.of(0, 1, 7)) {
            var patches = new LinkedHashMap<String, Object>();

            for (int i = 0; i < count; ++i) {
                patches.put(graph611.get(i).id(), Map.of("type", "file"));
            }

            var data = Map.<String, Object>of("patches", patches);
            var profile =
                    new ProfileRegistry.Profile(Api30NativeFiles.QINGGAN11_611, "test", data, "");
            denied = false;

            try {
                PreparationMain.nativeFileOutputs(
                        Map.of(), profile, Map.of(), Map.of(), (path, pin) -> path);
            } catch (Failure expected) {
                denied = "PREPARATION_NATIVE_FILE".equals(expected.code);
            }

            require(denied, "Free A11 6.11.1 accepted incomplete native graph of size " + count);
        }

        require(
                !Api30NativeFiles.forProfile("free-11-chinese-20250710-6.11.1-other")
                        .equals(graph611),
                "Similar profile selected Free A11 6.11.1 graph");
        System.out.println("Free A11 6.11.1 native graph and negative selection checks passed");
    }
}
