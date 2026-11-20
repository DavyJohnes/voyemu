package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.Arrays;
import java.util.Map;

public final class MediaGuardsTest {

    interface Check {
        void run() throws Exception;
    }

    static void rejects(Check c) throws Exception {
        try {
            c.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("Media guard accepted an incompatible input");
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        var steps = TestSteps.declared(root, MediaCloudConfigGuard.NAME);
        Map<String, Object> p = steps.get(0), other = steps.get(1);
        Path a611 = TestSteps.original(root, p, "QGMedia.apk"),
                a731 = TestSteps.original(root, other, "QGMedia.apk");
        var entries = NativeCore.entries(a611);
        byte[] raw = entries.get("classes3.dex"), manifest = entries.get("AndroidManifest.xml");
        String abi = "arm64-v8a";
        byte[] cloud = MediaCloudConfigGuard.apply(p, 30, abi, a611, raw);
        byte[] fixedManifest = MediaCloudManifestGuard.apply(p, 30, abi, a611, manifest);

        if (Arrays.equals(raw, cloud) || fixedManifest.length != manifest.length - 100) {
            throw new AssertionError("cloud guard did not apply");
        }

        if (MediaCloudConfigGuardTest.removals(raw) != 0
                || MediaCloudConfigGuardTest.removals(cloud) != 2) {
            throw new AssertionError("both cloud media entries must be removed");
        }

        checkManifestEdit(manifest, fixedManifest);
        UsbPlayerGuard.validateOriginal(p, 30, abi, a611, raw);
        byte[] usb = UsbPlayerGuard.apply(p, 30, abi, a611, cloud);

        if (Arrays.equals(cloud, usb)) {
            throw new AssertionError("USB guard did not apply");
        }

        var before = UsbPlayerGuardTest.classes(cloud);
        var after = UsbPlayerGuardTest.classes(usb);
        int changed = 0;

        if (!before.keySet().equals(after.keySet())) {
            throw new AssertionError("USB class inventory differs");
        }

        for (var name : before.keySet()) {
            if (!FrameworkDexMaterializer.semantic(UsbPlayerGuard.canonicalClass(before.get(name)))
                    .equals(
                            FrameworkDexMaterializer.semantic(
                                    UsbPlayerGuard.canonicalClass(after.get(name))))) {
                if (!UsbPlayerGuard.TARGETS.contains(name)) {
                    throw new AssertionError("unrelated USB class changed: " + name);
                }

                changed++;
            }
        }

        if (changed == 0) {
            throw new AssertionError("USB targets did not change");
        }

        rejects(() -> MediaCloudConfigGuard.apply(p, 30, abi, a731, raw));
        rejects(() -> MediaCloudConfigGuard.apply(other, 30, abi, a611, raw));
        rejects(() -> MediaCloudManifestGuard.apply(p, 30, abi, a731, manifest));
        rejects(() -> MediaCloudManifestGuard.apply(p, 30, abi, a611, fixedManifest));
    }

    static void checkManifestEdit(byte[] manifest, byte[] fixedManifest) {
        var oldXml = java.nio.ByteBuffer.wrap(manifest).order(java.nio.ByteOrder.LITTLE_ENDIAN);
        var newXml =
                java.nio.ByteBuffer.wrap(fixedManifest).order(java.nio.ByteOrder.LITTLE_ENDIAN);
        int removal = 14624, enabled = removal + 1056;

        if (Short.toUnsignedInt(oldXml.getShort(removal)) != 0x102
                || oldXml.getInt(removal + 4) != 76
                || Short.toUnsignedInt(oldXml.getShort(removal + 76)) != 0x103
                || oldXml.getInt(removal + 80) != 24
                || oldXml.getInt(enabled) != -1
                || newXml.getInt(enabled - 100) != 0) {
            throw new AssertionError("initializer removal or service disable differs");
        }
    }
}
