package com.voyemu.prepare;

public final class Api30OriginalNaviNativeSelectionTest {
    public static void main(String[] args) throws Exception {
        var original = Api30NativeFiles.forPlatform("qinggan11", true);

        checkNodeGraph(Api30NativeFiles.QINGGAN11_ORIGINAL_NAVI_SPECS, original);
        checkNodeSteps();

        if (Api30NativeFiles.forPlatform("qinggan11", false).contains(original.get(2))) {
            throw new AssertionError("The original Navi MAC node must follow its declaration");
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
            java.util.List<Api30NativeFiles.Spec> legacy,
            java.util.List<Api30NativeFiles.Spec> original)
            throws Exception {
        if (legacy.size() != 9 || original.size() != 9 || !legacy.equals(original)) {
            throw new AssertionError("Exact 9-node original Navi graph required");
        }

        var ids = new java.util.ArrayList<String>();
        original.forEach(spec -> ids.add(spec.id()));

        if (!ids.equals(
                java.util.List.of(
                        "keymanager-contexts-api30",
                        "public-qg-runtime-api30",
                        "original-navi-mac",
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
                                "d323725be43eb7b80539044878d4bdad51cb12e5992014bebf25749d43659e22")) {
            throw new AssertionError("9-node chain hash pin missing");
        }

        var contentDigest = "4bf19f6c8144488d129d7f280d66c3ffb0354a95fffcb6c0de6e6a413908efdc";
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
