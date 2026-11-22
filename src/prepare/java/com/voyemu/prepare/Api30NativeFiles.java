package com.voyemu.prepare;

import java.util.List;

/** Exact native outputs accepted from the registered API30 clean base; no file transforms. */
final class Api30NativeFiles {
    record Spec(String id, String target, String inputPin, String outputPin) {
        String step() {
            return id + ".json";
        }
    }

    static final List<Spec> SPECS =
            List.of(
                    new Spec(
                            "selinux-services-api30",
                            "/system/etc/selinux/plat_sepolicy.cil",
                            "f6d5316e443b6830d5331e6586cc0fc1df2ea8a59853ae3b0870945990145a17",
                            "ff445a22bb8718bed947ca97f05d091d0e62a87bb309165cc2c67945e7bd2d14"),
                    new Spec(
                            "selinux-service-contexts-api30",
                            "/system/etc/selinux/plat_service_contexts",
                            "e4a9f3406dc9c3ff5a03c4b478941e02bede17ee95798bdbce1a31cc33f40959",
                            "ac366650a5d9e011d299845155d1319a1f35ec92e107cc1cded0973fa945f1b5"),
                    new Spec(
                            "selinux-hash-api30",
                            "/system/etc/selinux/plat_sepolicy_and_mapping.sha256",
                            "391829e0167f44415e54ba64692ad410b1398b16263869beb22e427130eae766",
                            "99c1b908750a0a1320e160aa9b20dc0129c332e5667dbdb3c29f33982a55561a"));

    /** The qinggan11 graph of a profile with the original_navi feature. */
    static final List<Spec> QINGGAN11_ORIGINAL_NAVI_SPECS =
            List.of(
                    new Spec(
                            "keymanager-contexts-api30",
                            "/system/etc/selinux/plat_service_contexts",
                            "e4a9f3406dc9c3ff5a03c4b478941e02bede17ee95798bdbce1a31cc33f40959",
                            "d73bcfe6f82f18f52e40e86d55f2a1163d2b526a0143bee9322d0ea5da368061"),
                    new Spec(
                            "public-qg-runtime-api30",
                            "/system/etc/public.libraries.txt",
                            "34c43bf7099fc57daf2db5de07891eb20f76eafa6d9d8a4f4f01e0cefb48c070",
                            "61307bd17de318871f19ed5effede9e65204d7c510e246d91155cc7162744763"),
                    new Spec(
                            "original-navi-selinux",
                            "/system/etc/selinux/plat_mac_permissions.xml",
                            "34309b705a40fbc6a52a96cfc6713c90a683223e0652b936b7227584546169d8",
                            "c5c361ff9623c3c3798f60210684ae10b5eec48bed7e9e43a3b275f2b36db392"),
                    new Spec(
                            "can-simulator-contexts-api30",
                            "/system/etc/selinux/plat_file_contexts",
                            "cf0a4db0a9962781c83e6d1430c23276ebd406c404a54ab2226a1dfa4ac2844c",
                            "7fc4c203185a6036dafacc401472a152111b9d313702f28befb1327b5dab1f5e"),
                    new Spec(
                            "can-early-file-contexts-api30",
                            "/system/etc/selinux/plat_file_contexts",
                            "cf0a4db0a9962781c83e6d1430c23276ebd406c404a54ab2226a1dfa4ac2844c",
                            "873da6472b242a86d657900bdd90d4fa458dbde615c75ea406c6806f06ce8ecc"),
                    new Spec(
                            "can-simulator-policy-api30",
                            "/system/etc/selinux/plat_sepolicy.cil",
                            "f6d5316e443b6830d5331e6586cc0fc1df2ea8a59853ae3b0870945990145a17",
                            "da0a10c128b5c52c67e52795090755fd97fc5d02999c26d150bd9afa27d6fdb1"),
                    new Spec(
                            "can-early-policy-api30",
                            "/system/etc/selinux/plat_sepolicy.cil",
                            "f6d5316e443b6830d5331e6586cc0fc1df2ea8a59853ae3b0870945990145a17",
                            "e8f3b8cb54b3ec09280cf5c630f495b3202f1c9994e796c5a1cf707a5dcd6cf6"),
                    new Spec(
                            "can-early-property-contexts-api30",
                            "/system/etc/selinux/plat_property_contexts",
                            "a49213f01ea1f8ec19b41311116bc9324c682af1482297e4bbbc05265dfd623e",
                            "6618819c1a2cf856486d66e270459aeccfe03d8d7837c9cc946940cd5a104493"),
                    new Spec(
                            "selinux-hash-api30",
                            "/system/etc/selinux/plat_sepolicy_and_mapping.sha256",
                            "391829e0167f44415e54ba64692ad410b1398b16263869beb22e427130eae766",
                            "7293ab8b5ff6f3890a7f60b158e6bf28c882896c755edf1e996c9231519c37de"));

    private static final String
            POLICY = "50e7563396d73b053a36a9022d31807121731ecc0ce51b695df65ec7d6838db6",
            HASH = "2f158789774a59073cf85bf2c91272416b0da948eea037f9e13586187338f2c9";

    /**
     * The qinggan11 graph without the original Navi SELinux node; its early policy omits the su
     * read of mac_perms_file, which changes the policy and hash pins.
     */
    static final List<Spec> QINGGAN11_SPECS =
            QINGGAN11_ORIGINAL_NAVI_SPECS.stream()
                    .filter(spec -> !spec.id().equals("original-navi-selinux"))
                    .map(spec ->
                            switch (spec.id()) {
                                case "can-early-policy-api30" -> withOutput(spec, POLICY);
                                case "selinux-hash-api30" -> withOutput(spec, HASH);
                                default -> spec;
                            })
                    .toList();

    static final List<Spec> PASSION_SPECS =
            List.of(
                    new Spec(
                            "passion-services",
                            "/system/etc/selinux/plat_sepolicy.cil",
                            "f6d5316e443b6830d5331e6586cc0fc1df2ea8a59853ae3b0870945990145a17",
                            "2779bd1603edb26a28dbadfeacc7d1f4e7f4261a08b2526008e6b6d8b9b068e2"),
                    new Spec(
                            "passion-service-contexts",
                            "/system/etc/selinux/plat_service_contexts",
                            "e4a9f3406dc9c3ff5a03c4b478941e02bede17ee95798bdbce1a31cc33f40959",
                            "25c66b18abdb1bf102f5012387d54336c12a67c80e4ab540b90fdb863c1b82b6"),
                    new Spec(
                            "passion-property-contexts",
                            "/system/etc/selinux/plat_property_contexts",
                            "a49213f01ea1f8ec19b41311116bc9324c682af1482297e4bbbc05265dfd623e",
                            "1f322fc1fdea910bee266cc00126d2e8429b82a4a283250f12655e5d49b83d2c"),
                    new Spec(
                            "passion-hwservice-contexts",
                            "/system/etc/selinux/plat_hwservice_contexts",
                            "8b4e2ffcb1a9f8e56423961a561534dd56f9f6b35d26fc0be2b8492e86797c2e",
                            "2b2ebe82d3b754dc9a00d50de124d7c07ca7c847db91e918d8e1e08778d7ce19"),
                    new Spec(
                            "passion-file-contexts",
                            "/system/etc/selinux/plat_file_contexts",
                            "cf0a4db0a9962781c83e6d1430c23276ebd406c404a54ab2226a1dfa4ac2844c",
                            "fc78a9776ac31e4bf2d5b93c3ec7b32ddf4095c6e59949494b20bde76e207a2a"),
                    new Spec(
                            "passion-policy-hash",
                            "/system/etc/selinux/plat_sepolicy_and_mapping.sha256",
                            "391829e0167f44415e54ba64692ad410b1398b16263869beb22e427130eae766",
                            "5a4b81918614dfe32243876e85dd5cbd6d1a86d6da7ded7950b518383142f7dc"));

    /** Exact multi-step native recipe order mirrored with native_file_patch_steps. */
    static List<String> nodeSteps(String id) {
        return switch (id) {
            case "can-simulator-contexts-api30" -> List.of("can-simulator-contexts-api30.json");
            case "can-early-file-contexts-api30" ->
                    List.of(
                            "can-simulator-contexts-api30.json",
                            "can-early-file-contexts-api30.json");
            case "can-simulator-policy-api30" ->
                    List.of("selinux-services-api30.json", "can-simulator-policy-api30.json");
            case "can-early-policy-api30" ->
                    List.of(
                            "selinux-services-api30.json",
                            "can-simulator-policy-api30.json",
                            "can-early-policy-api30.json");
            case "can-early-property-contexts-api30" ->
                    List.of(
                            "property-contexts-api30.json",
                            "can-early-property-contexts-api30.json");
            default -> List.of(id + ".json");
        };
    }

    /**
     * The native graph of a profile platform; on qinggan11 the original Navi SELinux node joins
     * with the original_navi feature.
     */
    static List<Spec> forPlatform(Object platform, boolean originalNavi) {
        if ("qinggan11".equals(platform)) {
            return originalNavi ? QINGGAN11_ORIGINAL_NAVI_SPECS : QINGGAN11_SPECS;
        }

        return "passion".equals(platform) ? PASSION_SPECS : SPECS;
    }

    static List<Spec> forProfile(ProfileRegistry.Profile profile) {
        return forPlatform(profile.data().get("platform"), profile.originalNavi());
    }

    static final String MAPPING = "/system/etc/selinux/mapping/30.0.cil",
            MAPPING_PIN = "c14ab911584595a94e6783600371fed23cec1d56b8001383584a3192d028eb60";

    private static Spec withOutput(Spec spec, String outputPin) {
        return new Spec(spec.id(), spec.target(), spec.inputPin(), outputPin);
    }

    private Api30NativeFiles() {}
}
