package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.Set;
import java.util.TreeMap;

/**
 * Exact clean Android30 framework; the API28 wrapper's descriptor and instructions are identical.
 */
final class Api30ViewObservation {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    static boolean supports(String profile) {
        return PROFILE.equals(profile)
                || "free-11-chinese-20250710-6.11.1".equals(profile)
                || "passion-11-russian-20260403-2.1.4.80".equals(profile);
    }

    static final String PIN = "c17a2b45ae03bad051528fc1a9487a5c320b43b2fd4bf11c9b615cfda2ffe633";

    private static void require(boolean value) {
        Failure.require(
                value,
                "API30_VIEW_OBSERVATION_CONTRACT",
                "Exact registered clean Android30 framework bridge required",
                3);
    }

    static Path source(ProfileRegistry.Profile profile, java.util.Map<String, Object> component)
            throws Exception {
        require(
                supports(profile.id())
                        && profile.sourceRoot() != null
                        && profile.sourceRoot().isAbsolute()
                        && profile.sourceRoot().toRealPath().equals(profile.sourceRoot()));
        String relative = "profiles/free-09-chinese-20260106-6.0.3/components/view-observation/src";
        require(relative.equals(Json.str(component, "api30ViewObservation")));
        Path source = Storage.confined(profile.sourceRoot(), relative);
        require(
                FilesSupport.tree(source)
                        .keySet()
                        .equals(Set.of("com/voyemu/api/ViewDumpBridge.java")));

        return source;
    }

    static byte[] normalized(byte[] source) throws Exception {
        // The pinned Android30 dexlayout output keeps its pre-layout SHA1 signature.
        // Normalize header hashes only; code is untouched.
        byte[] checked = source.clone();
        byte[] signature =
                java.security.MessageDigest.getInstance("SHA-1")
                        .digest(java.util.Arrays.copyOfRange(checked, 32, checked.length));

        System.arraycopy(signature, 0, checked, 12, signature.length);
        var checksum = new java.util.zip.Adler32();
        checksum.update(checked, 12, checked.length - 12);
        java.nio.ByteBuffer.wrap(checked)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN)
                .putInt(8, (int) checksum.getValue());

        return checked;
    }

    static Path build(String profile, int api, Path original, Path helper, Path output)
            throws Exception {
        require(supports(profile) && api == 30 && FilesSupport.hash(original).equals(PIN));
        var entries = new TreeMap<>(NativeCore.entries(original));
        require(
                entries.keySet().stream()
                        .filter(name -> name.endsWith(".dex"))
                        .collect(java.util.stream.Collectors.toSet())
                        .equals(
                                Set.of(
                                        "classes.dex",
                                        "classes2.dex",
                                        "classes3.dex",
                                        "classes4.dex")));
        byte[] source = entries.get("classes3.dex");
        require(source != null);

        for (var entry : entries.entrySet()) {
            if (entry.getKey().endsWith(".dex")) {
                for (String descriptor :
                        NativeCore.classes(
                                new org.jf.dexlib2.dexbacked.DexBackedDexFile(
                                        org.jf.dexlib2.Opcodes.forApi(30), entry.getValue()))) {
                    require(
                            !descriptor.equals(ViewDumpPatch.HELPER)
                                    && !descriptor.startsWith(
                                            ViewDumpPatch.HELPER.substring(
                                                            0, ViewDumpPatch.HELPER.length() - 1)
                                                    + "$"));
                }
            }
        }

        var donor = NativeCore.entries(helper);
        require(donor.keySet().equals(Set.of("classes.dex")));
        byte[] bridge = FrameworkBridgeVerifier.jumboHelper(donor.get("classes.dex"));
        byte[] checked = normalized(source);
        byte[] patched =
                ViewDumpPatch.patch(
                        checked, NativeCore.hash(checked), bridge, NativeCore.hash(bridge), 30);
        patched = Api30HiddenApiMetadata.restore(source, patched);
        ViewDumpPatch.checkedDex(patched, NativeCore.hash(patched));
        FrameworkBridgeVerifier.verify(source, bridge, patched);
        entries.put("classes3.dex", patched);
        NativeCore.zip(output, entries, true);

        return output;
    }
}
