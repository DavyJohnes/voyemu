package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.Arrays;
import java.util.TreeMap;

public final class GalleryArm64RoundTripTest {
    static final String PROFILE = "free-11-chinese-20250710-6.11.1";

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
                        ".local/profiles/free-11-chinese-20250710-6.11.1/originals/Gallery.apk");
        var baseline = NativeCore.entries(original);

        baseline.keySet().removeIf(DexOperations::signatureEntry);
        var expected = GalleryArm64Libraries.selected(source);
        Path component = GalleryArm64Libraries.build(PROFILE, 30, "arm64-v8a", source, out);
        var patched =
                GalleryArm64Libraries.add(
                        PROFILE, 30, "arm64-v8a", original, baseline, component, source);

        if (expected.size() != 10
                || patched.size() != baseline.size() + 10
                || !GalleryArm64Libraries.same(
                        baseline,
                        GalleryArm64Libraries.remove(
                                PROFILE, 30, "arm64-v8a", original, patched, component, source))) {
            throw new AssertionError("Gallery round trip differs");
        }

        for (var e : baseline.entrySet()) {
            if (!Arrays.equals(e.getValue(), patched.get(e.getKey()))) {
                throw new AssertionError("Original entry changed");
            }
        }

        reject(() -> GalleryArm64Libraries.context("foreign", 30, "arm64-v8a"));
        reject(() -> GalleryArm64Libraries.context(PROFILE, 28, "arm64-v8a"));
        reject(() -> GalleryArm64Libraries.context(PROFILE, 30, "armeabi-v7a"));
        Path other =
                root.resolve(
                        ".local/profiles/free-11-chinese-20260731-7.3.1/originals/Gallery.apk");
        reject(() ->
                GalleryArm64Libraries.add(
                        PROFILE, 30, "arm64-v8a", other, baseline, component, source));
        var damaged = new TreeMap<>(patched);
        damaged.put(expected.keySet().iterator().next(), new byte[20]);
        reject(() ->
                GalleryArm64Libraries.remove(
                        PROFILE, 30, "arm64-v8a", original, damaged, component, source));
        System.out.println(
                "Free A11 6.11.1 Gallery ARM64 recipe round-trip and cross-version checks passed");
    }
}
