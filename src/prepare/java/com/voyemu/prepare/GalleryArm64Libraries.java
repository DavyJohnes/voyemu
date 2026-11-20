package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/** Exact official ARM64 additions of the profile; preserves every original OEM entry. */
final class GalleryArm64Libraries {
    static final String NAME = "api30-gallery-arm64";

    static void need(boolean value) {
        Failure.require(value, "GALLERY_ARM64", "Exact Gallery ARM64 contract required", 3);
    }

    static void context(int api, String abi) {
        need(api == 30 && abi.equals("arm64-v8a"));
    }

    static boolean validUrl(String value) {
        try {
            var u = java.net.URI.create(value);

            return "https".equals(u.getScheme())
                    && u.getHost() != null
                    && u.getUserInfo() == null
                    && u.getFragment() == null;
        } catch (IllegalArgumentException invalid) {
            return false;
        }
    }

    static Map<String, Object> definition(Path source) throws Exception {
        var d =
                Json.object(
                        Json.parse(Files.readString(Storage.confined(source, "component.json"))));

        need(
                d.keySet()
                                .equals(
                                        Set.of(
                                                "type",
                                                "minSdk",
                                                "output",
                                                "externalSources",
                                                "libraries",
                                                "provenance"))
                        && d.get("type").equals("native-library-archive")
                        && Json.integer(d, "minSdk") == 30
                        && d.get("output").equals("gallery-arm64.zip"));
        var external = Json.obj(d, "externalSources");
        need(external.keySet().equals(Set.of("ijk.aar", "fmmr.aar")));

        for (var value : external.values()) {
            var p = Json.object(value);
            need(
                    p.keySet().equals(Set.of("kind", "format", "url", "sha256", "bytes"))
                            && p.get("kind").equals("archive")
                            && p.get("format").equals("aar")
                            && validUrl(Json.str(p, "url"))
                            && Json.str(p, "sha256").matches("[0-9a-f]{64}")
                            && Json.integer(p, "bytes") > 0
                            && Json.integer(p, "bytes") <= 16L * 1024 * 1024);
        }

        need(Json.obj(d, "libraries").size() == 10);

        return d;
    }

    static Map<String, byte[]> selected(Path source) throws Exception {
        var d = definition(source);
        var external = Json.obj(d, "externalSources");
        var archives = new TreeMap<String, Map<String, byte[]>>();

        for (var e : external.entrySet()) {
            var pin = Json.object(e.getValue());
            Path p = Storage.confined(source, e.getKey());
            need(
                    Files.isRegularFile(p)
                            && !Files.isSymbolicLink(p)
                            && Files.size(p) == Json.integer(pin, "bytes")
                            && FilesSupport.hash(p).equals(Json.str(pin, "sha256")));
            archives.put(e.getKey(), NativeCore.entries(p));
        }

        var result = new TreeMap<String, byte[]>();

        for (var e : Json.obj(d, "libraries").entrySet()) {
            String name = e.getKey();
            need(name.matches("lib/arm64-v8a/lib[a-z0-9_]+\\.so"));
            var pin = Json.object(e.getValue());
            need(
                    pin.keySet().equals(Set.of("archive", "entry", "sha256", "bytes"))
                            && Json.str(pin, "entry")
                                    .equals(
                                            "jni/arm64-v8a/"
                                                    + name.substring(name.lastIndexOf('/') + 1)));
            var archive = archives.get(Json.str(pin, "archive"));
            need(archive != null);
            byte[] bytes = archive.get(Json.str(pin, "entry"));
            need(
                    bytes != null
                            && bytes.length == Json.integer(pin, "bytes")
                            && NativeCore.hash(bytes).equals(Json.str(pin, "sha256"))
                            && bytes.length >= 20
                            && bytes[0] == 127
                            && bytes[1] == 'E'
                            && bytes[2] == 'L'
                            && bytes[3] == 'F'
                            && bytes[4] == 2
                            && bytes[5] == 1
                            && Byte.toUnsignedInt(bytes[18]) == 183
                            && bytes[19] == 0);
            need(result.put(name, bytes) == null);
        }

        need(result.size() == 10);

        return result;
    }

    static Path build(int api, String abi, Path source, Path dir) throws Exception {
        context(api, abi);
        var entries = selected(source);
        Path output = Storage.confined(dir, "gallery-arm64.zip");
        need(!Files.exists(output));
        NativeCore.zip(output, entries, false);
        need(same(entries, NativeCore.entries(output)));

        return output;
    }

    static boolean same(Map<String, byte[]> a, Map<String, byte[]> b) {
        return a.keySet().equals(b.keySet())
                && a.entrySet().stream()
                        .allMatch(e -> Arrays.equals(e.getValue(), b.get(e.getKey())));
    }

    static Map<String, byte[]> add(
            Map<String, Object> constants,
            int api,
            String abi,
            Path original,
            Map<String, byte[]> input,
            Path component,
            Path source)
            throws Exception {
        var entries = entries(constants, api, abi, original, input, component, source);

        need(same(entries.current(), entries.baseline()));

        for (var e : entries.additions().entrySet()) {
            need(entries.current().putIfAbsent(e.getKey(), e.getValue()) == null);
        }

        return entries.current();
    }

    static Map<String, byte[]> remove(
            Map<String, Object> constants,
            int api,
            String abi,
            Path original,
            Map<String, byte[]> input,
            Path component,
            Path source)
            throws Exception {
        var entries = entries(constants, api, abi, original, input, component, source);

        for (var e : entries.additions().entrySet()) {
            need(Arrays.equals(entries.current().get(e.getKey()), e.getValue()));
            entries.current().remove(e.getKey());
        }

        need(same(entries.current(), entries.baseline()));

        return entries.current();
    }

    private record Entries(
            Map<String, byte[]> additions,
            Map<String, byte[]> baseline,
            Map<String, byte[]> current) {}

    private static Entries entries(
            Map<String, Object> constants,
            int api,
            String abi,
            Path original,
            Map<String, byte[]> input,
            Path component,
            Path source)
            throws Exception {
        context(api, abi);
        need(StepConstants.admits(constants, FilesSupport.hash(original)));
        var expected = selected(source);
        var additions = NativeCore.entries(component);
        need(same(expected, additions));
        var baseline = NativeCore.entries(original);
        baseline.keySet().removeIf(DexOperations::signatureEntry);
        var current = new TreeMap<>(input);
        current.keySet().removeIf(DexOperations::signatureEntry);
        need(baseline.keySet().stream().noneMatch(n -> n.startsWith("lib/arm64-v8a/")));

        return new Entries(additions, baseline, current);
    }
}
