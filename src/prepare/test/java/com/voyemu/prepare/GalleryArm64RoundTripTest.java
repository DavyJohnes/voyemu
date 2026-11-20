package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.Arrays;
import java.util.Map;
import java.util.TreeMap;

public final class GalleryArm64RoundTripTest {
    /** Pins of two Gallery originals of different firmware. */
    static final String PIN = "f716384510601971d1dc80a45304aac09e03530e5073c1497255b2cb810aae30",
            OTHER = "c30633d39bb4127ac7fcd79300f00f2be608ff6b59cb24ebf855471e967be537";

    interface Check {
        void run() throws Exception;
    }

    static void reject(Check c) throws Exception {
        boolean denied = false;

        try {
            c.run();
        } catch (Failure | IllegalArgumentException expected) {
            denied = true;
        }

        if (!denied) {
            throw new AssertionError("Invalid Gallery input accepted");
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), source = Path.of(args[1]), out = Path.of(args[2]);
        Path original =
                root.resolve(
                        ".local/profiles/"
                                + TestProfiles.pinning(root, PIN)
                                + "/originals/Gallery.apk");
        Map<String, Object> constants =
                TestProfiles.constants(root, GalleryArm64Libraries.NAME, PIN);
        var baseline = NativeCore.entries(original);

        baseline.keySet().removeIf(DexOperations::signatureEntry);
        var expected = GalleryArm64Libraries.selected(source);
        Path component = GalleryArm64Libraries.build(30, "arm64-v8a", source, out);
        var patched =
                GalleryArm64Libraries.add(
                        constants, 30, "arm64-v8a", original, baseline, component, source);

        if (expected.size() != 10
                || patched.size() != baseline.size() + 10
                || !GalleryArm64Libraries.same(
                        baseline,
                        GalleryArm64Libraries.remove(
                                constants,
                                30,
                                "arm64-v8a",
                                original,
                                patched,
                                component,
                                source))) {
            throw new AssertionError("Gallery round trip differs");
        }

        for (var e : baseline.entrySet()) {
            if (!Arrays.equals(e.getValue(), patched.get(e.getKey()))) {
                throw new AssertionError("Original entry changed");
            }
        }

        reject(() -> GalleryArm64Libraries.context(28, "arm64-v8a"));
        reject(() -> GalleryArm64Libraries.context(30, "armeabi-v7a"));
        Path other =
                root.resolve(
                        ".local/profiles/"
                                + TestProfiles.pinning(root, OTHER)
                                + "/originals/Gallery.apk");
        reject(() ->
                GalleryArm64Libraries.add(
                        constants, 30, "arm64-v8a", other, baseline, component, source));
        var damaged = new TreeMap<>(patched);
        damaged.put(expected.keySet().iterator().next(), new byte[20]);
        reject(() ->
                GalleryArm64Libraries.remove(
                        constants, 30, "arm64-v8a", original, damaged, component, source));
        System.out.println("Gallery ARM64 recipe round-trip and cross-version checks passed");
    }
}
