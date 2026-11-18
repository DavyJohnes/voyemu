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
    static final String QINGGAN11_731 = "free-11-chinese-20260731-7.3.1";
    static final List<Spec> QINGGAN11_731_SPECS =
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
                            "original-navi-mac",
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
                            "123f8180bca7499a8b837f5c4cd5b97e11ca216fa4a370179d1dc7624f9dd7b1"),
                    new Spec(
                            "can-early-property-contexts-api30",
                            "/system/etc/selinux/plat_property_contexts",
                            "a49213f01ea1f8ec19b41311116bc9324c682af1482297e4bbbc05265dfd623e",
                            "6618819c1a2cf856486d66e270459aeccfe03d8d7837c9cc946940cd5a104493"),
                    new Spec(
                            "selinux-hash-api30",
                            "/system/etc/selinux/plat_sepolicy_and_mapping.sha256",
                            "391829e0167f44415e54ba64692ad410b1398b16263869beb22e427130eae766",
                            "d323725be43eb7b80539044878d4bdad51cb12e5992014bebf25749d43659e22"));
    static final String QINGGAN11_611 = "free-11-chinese-20250710-6.11.1";
    static final List<Spec> QINGGAN11_611_SPECS =
            QINGGAN11_731_SPECS.stream()
                    .filter(spec -> !spec.id().equals("original-navi-mac"))
                    .toList();
    static final String PASSION = "passion-11-russian-20260403-2.1.4.80";
    static final List<Spec> PASSION_SPECS =
            List.of(
                    new Spec(
                            "passion-services",
                            "/system/etc/selinux/plat_sepolicy.cil",
                            "f6d5316e443b6830d5331e6586cc0fc1df2ea8a59853ae3b0870945990145a17",
                            "008fade9e6f183e228e7210a7bc9c30750c45f83ecfdb932ad92f61f0eeaadc4"),
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
                            "passion-policy-hash",
                            "/system/etc/selinux/plat_sepolicy_and_mapping.sha256",
                            "391829e0167f44415e54ba64692ad410b1398b16263869beb22e427130eae766",
                            "243e1ba72d2c8e67417aa9443424c2063732ca932ce9add8530e4fdc907c2f10"));

    static List<Spec> forProfile(String profile) {
        return QINGGAN11_731.equals(profile)
                ? QINGGAN11_731_SPECS
                : QINGGAN11_611.equals(profile)
                        ? QINGGAN11_611_SPECS
                        : PASSION.equals(profile) ? PASSION_SPECS : SPECS;
    }

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

    static List<Spec> forProfile(String profile, boolean originalNavi) {
        if (QINGGAN11_611.equals(profile)) {
            if (originalNavi) {
                throw new IllegalStateException(
                        "Free A11 6.11.1 native graph does not include original Navi MAC");
            }

            return QINGGAN11_611_SPECS;
        }

        if (!QINGGAN11_731.equals(profile)) {
            return forProfile(profile);
        }

        if (!originalNavi) {
            throw new IllegalStateException(
                    "Free A11 7.3.1 native graph requires original Navi MAC");
        }

        return QINGGAN11_731_SPECS;
    }

    static final String MAPPING = "/system/etc/selinux/mapping/30.0.cil",
            MAPPING_PIN = "c14ab911584595a94e6783600371fed23cec1d56b8001383584a3192d028eb60";

    private Api30NativeFiles() {}
}
