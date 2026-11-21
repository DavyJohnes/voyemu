package com.voyemu.prepare;

public final class Api30OriginalNaviNativeSelectionTest {
    public static void main(String[] args) throws Exception {
        var original = Api30NativeFiles.forPlatform("qinggan11", true);

        checkNodeGraph(Api30NativeFiles.forPlatform("qinggan11", false), original);
        checkNodeSteps();

        if (Api30NativeFiles.forPlatform("qinggan11", false).contains(original.get(2))) {
            throw new AssertionError("The original Navi SELinux node must follow its declaration");
        }

        for (String platform : java.util.List.of("passion", "qinggan09")) {
            if (!Api30NativeFiles.forPlatform(platform, true)
                    .equals(Api30NativeFiles.forPlatform(platform, false))) {
                throw new AssertionError("Other platform changed");
            }
        }

        System.out.println(
                "PASS original Navi native 9-node selection; step chains and foreign graphs"
                        + " unchanged");
    }

    private static void checkNodeGraph(
            java.util.List<Api30NativeFiles.Spec> common,
            java.util.List<Api30NativeFiles.Spec> original)
            throws Exception {
        // The su read of mac_perms_file joins the early policy only with the MAC node.
        var policyChain = java.util.Set.of("can-early-policy-api30", "selinux-hash-api30");

        if (original.size() != 9
                || !common.stream()
                        .filter(spec -> !policyChain.contains(spec.id()))
                        .toList()
                        .equals(
                                original.stream()
                                        .filter(spec ->
                                                !spec.id().equals("original-navi-selinux")
                                                        && !policyChain.contains(spec.id()))
                                        .toList())
                || common.size() != 8
                || common.stream()
                        .filter(spec -> policyChain.contains(spec.id()))
                        .anyMatch(original::contains)) {
            throw new AssertionError("Exact 9-node original Navi graph required");
        }

        var ids = new java.util.ArrayList<String>();
        original.forEach(spec -> ids.add(spec.id()));

        if (!ids.equals(
                java.util.List.of(
                        "keymanager-contexts-api30",
                        "public-qg-runtime-api30",
                        "original-navi-selinux",
                        "can-simulator-contexts-api30",
                        "can-early-file-contexts-api30",
                        "can-simulator-policy-api30",
                        "can-early-policy-api30",
                        "can-early-property-contexts-api30",
                        "selinux-hash-api30"))) {
            throw new AssertionError("9-node order changed");
        }

        var mac = original.get(2);

        if (!mac.target().equals("/system/etc/selinux/plat_mac_permissions.xml")
                || !mac.outputPin()
                        .equals(
                                "c5c361ff9623c3c3798f60210684ae10b5eec48bed7e9e43a3b275f2b36db392")) {
            throw new AssertionError("Pinned MAC node missing");
        }

        var hash = original.get(8);

        if (!hash.id().equals("selinux-hash-api30")
                || !hash.outputPin()
                        .equals(
                                "7293ab8b5ff6f3890a7f60b158e6bf28c882896c755edf1e996c9231519c37de")) {
            throw new AssertionError("9-node chain hash pin missing");
        }

        var contentDigest = "8b7d6819e4429cec7a98a86d31366d6b7fc75ae03033d418b15f84ec1f9161c3";
        var actualFilePin =
                java.util.HexFormat.of()
                        .formatHex(
                                java.security.MessageDigest.getInstance("SHA-256")
                                        .digest(
                                                (contentDigest + "\n")
                                                        .getBytes(
                                                                java.nio.charset.StandardCharsets
                                                                        .US_ASCII)));

        if (!hash.outputPin().equals(actualFilePin) || hash.outputPin().equals(contentDigest)) {
            throw new AssertionError(
                    "Combined content digest differs from SHA of final-LF hash file");
        }
    }

    private static void checkNodeSteps() {
        if (!Api30NativeFiles.nodeSteps("can-simulator-contexts-api30")
                .equals(java.util.List.of("can-simulator-contexts-api30.json"))) {
            throw new AssertionError("simulator contexts steps");
        }

        if (!Api30NativeFiles.nodeSteps("can-early-file-contexts-api30")
                .equals(
                        java.util.List.of(
                                "can-simulator-contexts-api30.json",
                                "can-early-file-contexts-api30.json"))) {
            throw new AssertionError("early contexts steps");
        }

        if (!Api30NativeFiles.nodeSteps("can-simulator-policy-api30")
                .equals(
                        java.util.List.of(
                                "selinux-services-api30.json",
                                "can-simulator-policy-api30.json"))) {
            throw new AssertionError("simulator policy steps");
        }

        if (!Api30NativeFiles.nodeSteps("can-early-policy-api30")
                .equals(
                        java.util.List.of(
                                "selinux-services-api30.json",
                                "can-simulator-policy-api30.json",
                                "can-early-policy-api30.json"))) {
            throw new AssertionError("early policy steps");
        }

        if (!Api30NativeFiles.nodeSteps("can-early-property-contexts-api30")
                .equals(
                        java.util.List.of(
                                "property-contexts-api30.json",
                                "can-early-property-contexts-api30.json"))) {
            throw new AssertionError("early property steps");
        }
    }
}
