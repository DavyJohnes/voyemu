package com.voyemu.prepare;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public final class NativeFilesTest {
    private static final String FREE_611 = "free-11-chinese-20250710-6.11.1";
    private static final String FREE_RC1 = "free-11-russian-20260318-rc1";

    private static void require(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    private static ProfileRegistry.Profile qinggan11(String id, Map<String, Object> patches) {
        return new ProfileRegistry.Profile(
                id, id, Map.of("platform", "qinggan11", "board", "8155", "patches", patches), "");
    }

    public static void main(String[] args) throws Exception {
        var graph611 = Api30NativeFiles.forProfile(qinggan11(FREE_611, Map.of()));
        var graphWithoutNavi = Api30NativeFiles.forPlatform("qinggan11", false);
        var graph731 = Api30NativeFiles.forPlatform("qinggan11", true);

        require(
                graph611.size() == 8 && graph731.size() == 9,
                "Exact Free native graph sizes required");
        require(graphWithoutNavi.equals(graph611), "qinggan11 non-Navi graph differs");
        require(
                graph731.stream()
                        .filter(spec -> !graph611.contains(spec))
                        .map(Api30NativeFiles.Spec::id)
                        .toList()
                        .equals(
                                List.of(
                                        "original-navi-selinux",
                                        "can-early-policy-api30",
                                        "selinux-hash-api30")),
                "Only the Navi SELinux node, the policy pin and sidecar may differ");
        require(
                graph731.stream()
                        .map(Api30NativeFiles.Spec::id)
                        .filter(id -> !id.equals("original-navi-selinux"))
                        .toList()
                        .equals(graph611.stream().map(Api30NativeFiles.Spec::id).toList()),
                "qinggan11 non-Navi node order differs");
        require(
                Api30NativeFiles.forProfile(qinggan11(FREE_RC1, Map.of())).equals(graph611),
                "RC1 must select the common graph");
        var featured =
                new ProfileRegistry.Profile(
                        TestProfiles.SYNTHETIC,
                        "test",
                        Map.of(
                                "platform",
                                "qinggan11",
                                "board",
                                "8155",
                                "features",
                                Map.of("original_navi", true),
                                "patches",
                                Map.of()),
                        "");
        require(
                Api30NativeFiles.forProfile(featured).equals(graph731),
                "The original_navi feature must select the original Navi SELinux graph");
        require(
                Api30NativeFiles.forProfile(
                                qinggan11(
                                        TestProfiles.SYNTHETIC,
                                        Map.of("original-navi-selinux", Map.of("type", "file"))))
                        .equals(graph611),
                "A step name without the original_navi feature must not select Navi");
        require(
                graph611.stream().noneMatch(spec -> spec.id().equals("original-navi-selinux")),
                "qinggan11 without original Navi must not select Navi SELinux");
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
        checkObserverPolicy(featured, graph731);
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

    private static void checkObserverPolicy(
            ProfileRegistry.Profile featured, List<Api30NativeFiles.Spec> graph731) {
        var observerData = new LinkedHashMap<String, Object>(featured.data());

        observerData.put(
                "features",
                Map.of(
                        "original_navi",
                        true,
                        "ui_observer_required",
                        true,
                        "ui_observer_root_accessibility",
                        true));
        observerData.put(
                "android",
                Map.of(
                        "package",
                        "system-images;android-30;default;arm64-v8a",
                        "development_kernel",
                        Map.of("abi", "arm64-v8a")));
        var observerProfile =
                new ProfileRegistry.Profile(TestProfiles.SYNTHETIC, "test", observerData, "");
        var observerGraph = Api30NativeFiles.forProfile(observerProfile);
        require(observerGraph.size() == graph731.size(), "Observer graph changed node inventory");
        require(
                observerGraph.stream()
                        .filter(spec -> !graph731.contains(spec))
                        .map(Api30NativeFiles.Spec::id)
                        .toList()
                        .equals(List.of("can-early-policy-api30", "selinux-hash-api30")),
                "Observer graph changes only policy and derived digest");

        for (int mutation = 0; mutation < 5; mutation++) {
            var changed = new LinkedHashMap<>(observerData);

            switch (mutation) {
                case 0 -> changed.put("platform", "passion");
                case 1 -> changed.put("board", "nxp");
                case 2 ->
                        changed.put(
                                "features",
                                Map.of(
                                        "ui_observer_root_accessibility",
                                        true,
                                        "ui_observer_required",
                                        true));
                case 3 -> changed.put("android", Map.of("package", "other"));
                default ->
                        changed.put("features", Map.of("ui_observer_root_accessibility", "true"));
            }

            boolean rejected = false;

            try {
                Api30NativeFiles.forProfile(
                        new ProfileRegistry.Profile(TestProfiles.SYNTHETIC, "test", changed, ""));
            } catch (Failure failure) {
                rejected = "SELINUX_PATCH_SCOPE".equals(failure.code);
            }

            require(rejected, "Observer graph accepted another scope or malformed opt-in");
        }
    }
}
