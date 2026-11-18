package com.voyemu.prepare;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.zip.ZipFile;

/**
 * Own guest Java of the profiles. The android-java recipe compiles every unit with javac and D8
 * into entries named by the SHA-256 of the unit inputs in android-java.zip; preparation reads the
 * DEX of a unit from that archive and compiles it itself only on a JDK when no archive is given.
 */
final class GuestJava {
    static final String COMPILE_ONLY = "compile-only";
    private static Path asset;

    /**
     * Java sources under root, declarations under root/compileOnly that compile only, optional
     * generated sources and the minimum API of the guest.
     */
    record Unit(Path root, String compileOnly, Path generated, int api) {
        Unit(Path root, int api) {
            this(root, COMPILE_ONLY, null, api);
        }
    }

    private GuestJava() {}

    /** Selects the CI-built android-java.zip; null compiles every unit on this JDK. */
    static void use(Path archive) {
        asset = archive;
    }

    // Every file under the unit root and the generated root by relative path and SHA-256, the
    // compile-only directory, the minimum API and the platform JAR the sources compile against.
    static String key(Unit unit, Path platform) throws Exception {
        StringBuilder text = new StringBuilder("voyemu-guest-java 1\n");

        text.append("api ").append(unit.api()).append('\n');
        text.append("platform ").append(FilesSupport.hash(platform)).append('\n');
        text.append("compile-only ").append(unit.compileOnly()).append('\n');
        append(text, "source", unit.root());

        if (unit.generated() != null && Files.exists(unit.generated())) {
            append(text, "generated", unit.generated());
        }

        return HexFormat.of()
                .formatHex(
                        MessageDigest.getInstance("SHA-256")
                                .digest(text.toString().getBytes(StandardCharsets.UTF_8)));
    }

    private static void append(StringBuilder text, String kind, Path root) throws Exception {
        for (var file : FilesSupport.tree(root).entrySet()) {
            text.append(kind)
                    .append(' ')
                    .append(file.getKey())
                    .append(' ')
                    .append(FilesSupport.hash(file.getValue()))
                    .append('\n');
        }
    }

    /** The relative paths of the files under root outside its compile-only declarations. */
    static Set<String> sources(Path root) throws Exception {
        Set<String> names = new TreeSet<>(FilesSupport.tree(root).keySet());

        names.removeIf(name -> name.startsWith(COMPILE_ONLY + "/"));

        return names;
    }

    /** Compiles the unit with the JDK into work/dex and returns that directory. */
    static Path compile(Unit unit, Path platform, Path work) throws Exception {
        Path declarations = unit.root().resolve(unit.compileOnly());

        return DexOperations.javaDex(
                unit.root(),
                unit.generated(),
                List.of(),
                platform,
                work,
                unit.api(),
                Files.isDirectory(declarations) ? declarations : null);
    }

    /** Returns the directory with the classes*.dex of the unit. */
    static Path dex(Unit unit, Path platform, Path work) throws Exception {
        if (asset == null) {
            return compile(unit, platform, work);
        }

        String key = key(unit, platform);
        Map<String, byte[]> dex = new TreeMap<>();

        try (var zip = new ZipFile(asset.toFile())) {
            for (var entry : java.util.Collections.list(zip.entries())) {
                String name = entry.getName();

                if (name.startsWith(key + "/")) {
                    String file = name.substring(key.length() + 1);
                    Failure.require(
                            file.matches("classes[0-9]*\\.dex"),
                            "ANDROID_JAVA_INVALID",
                            "Unexpected android-java entry " + name,
                            5);
                    dex.put(file, zip.getInputStream(entry).readAllBytes());
                }
            }
        }

        Failure.require(
                !dex.isEmpty(),
                "ANDROID_JAVA_MISSING",
                "The android-java asset has no DEX of "
                        + unit.root().getFileName()
                        + " (unit "
                        + key
                        + "); install the asset built from these sources or build with a JDK",
                3);
        Failure.require(
                dex.containsKey("classes.dex"),
                "ANDROID_JAVA_INVALID",
                "The android-java unit " + key + " has no primary DEX",
                5);
        Path output = work.resolve("dex");
        Storage.directory(output);

        for (var entry : dex.entrySet()) {
            Files.write(output.resolve(entry.getKey()), entry.getValue());
        }

        return output;
    }
}
