package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.TreeMap;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class PassionMusicDayDrawableGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static void deny(Action action) throws Exception {
        try {
            action.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("unexpected resource guard acceptance");
    }

    public static void main(String[] args) throws Exception {
        Path original = Path.of(args[0]), out = Path.of(args[1]);
        var source = NativeCore.entries(original);
        byte[] originalTable = source.get("resources.arsc");

        // Original RED: all three non-night selector dependencies have no default entry.
        for (int id : PassionMusicDayDrawableGuard.IDS) {
            int at =
                    PassionMusicDayDrawableGuard.DAY + PassionMusicDayDrawableGuard.HEADER + id * 4;

            if (java.nio.ByteBuffer.wrap(originalTable, at, 4)
                            .order(java.nio.ByteOrder.LITTLE_ENDIAN)
                            .getInt()
                    != -1) {
                throw new AssertionError();
            }
        }

        byte[] result =
                PassionMusicDayDrawableGuard.apply(TestProfiles.SYNTHETIC, 30, original, source);

        if (result.length != originalTable.length + 48) {
            throw new AssertionError();
        }

        deny(() ->
                PassionMusicDayDrawableGuard.apply(TestProfiles.SYNTHETIC, 28, original, source));
        var changed = new TreeMap<String, byte[]>(source);
        changed.put("resources.arsc", result);
        deny(() ->
                PassionMusicDayDrawableGuard.apply(TestProfiles.SYNTHETIC, 30, original, changed));
        changed.put("resources.arsc", originalTable);

        for (String name : PassionMusicDayDrawableGuard.NAMES) {
            String asset = "res/drawable-night-v8/" + name + ".webp";
            byte[] saved = changed.remove(asset);
            deny(() ->
                    PassionMusicDayDrawableGuard.apply(
                            TestProfiles.SYNTHETIC, 30, original, changed));
            changed.put(asset, saved);
        }

        changed.remove("res/drawable/selector_seekbar_thumb.xml");
        deny(() ->
                PassionMusicDayDrawableGuard.apply(TestProfiles.SYNTHETIC, 30, original, changed));
        var probe = new TreeMap<String, byte[]>();
        probe.put("AndroidManifest.xml", source.get("AndroidManifest.xml"));
        probe.put("resources.arsc", result);

        for (String name : PassionMusicDayDrawableGuard.NAMES) {
            String asset = "res/drawable-night-v8/" + name + ".webp";
            probe.put(asset, source.get(asset));
        }

        try (var stream =
                new ZipOutputStream(Files.newOutputStream(out, StandardOpenOption.CREATE_NEW))) {
            for (var item : probe.entrySet()) {
                stream.putNextEntry(new ZipEntry(item.getKey()));
                stream.write(item.getValue());
                stream.closeEntry();
            }
        }

        System.out.println(
                "Original RED missing default3 -> GREEN exact48B table extension;11negative"
                        + " guards;full inverse exact;OEMassets unchanged;host-only noninstallable"
                        + " resource probe");
    }
}
