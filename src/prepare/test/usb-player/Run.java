import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.net.URLClassLoader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HexFormat;

import javax.tools.ToolProvider;

/** JDK-only host execution; no Android SDK or external dependencies. */
class Run {
    private static final String HELPER =
            "profiles/free-11-chinese-20260731-7.3.1/patches/api30-usb-player/src/com/voyemu/oem/media/AndroidUsbPlayer.java";

    public static void main(String[] args) throws Exception {
        Path root =
                args.length == 1 ? Path.of(args[0]).toAbsolutePath() : Path.of("").toAbsolutePath();
        Path fixtures = root.resolve("src/prepare/test/usb-player/src");
        Path helper = root.resolve(HELPER);

        if (!Files.isDirectory(fixtures) || !Files.isRegularFile(helper)) {
            throw new IllegalArgumentException("Pass the checkout root as the only argument");
        }

        String source = Files.readString(helper);
        System.out.println(
                "Helper SHA256: "
                        + HexFormat.of()
                                .formatHex(
                                        MessageDigest.getInstance("SHA-256")
                                                .digest(source.getBytes(StandardCharsets.UTF_8))));
        Path work = Files.createTempDirectory("voyemu-usb-lifecycle-");

        try {
            execute(fixtures, helper, work.resolve("green"));
            String[][] mutations = {
                {
                    "early-prepared",
                    "player.prepareAsync();",
                    "player.prepareAsync(); notifyOnPrepared();"
                },
                {
                    "reset-drops-listeners",
                    "player.reset();\n        source = null;",
                    "player.reset();\n        resetListeners();\n        source = null;"
                },
                {
                    "release-keeps-listeners",
                    "player.release();\n        resetListeners();\n        source = null;",
                    "player.release();\n        source = null;"
                },
                {
                    "error-throw-completes",
                    "return true;\n            }",
                    "return false;\n            }"
                }
            };

            for (String[] mutation : mutations) {
                int at = source.indexOf(mutation[1]);

                if (at < 0 || source.indexOf(mutation[1], at + 1) >= 0) {
                    throw new AssertionError("Mutation anchor changed: " + mutation[0]);
                }

                Path dir = Files.createDirectories(work.resolve(mutation[0]));
                Path mutant = dir.resolve("AndroidUsbPlayer.java");
                Files.writeString(
                        mutant,
                        source.substring(0, at)
                                + mutation[2]
                                + source.substring(at + mutation[1].length()));

                try {
                    execute(fixtures, mutant, dir.resolve("classes"));
                } catch (InvocationTargetException failure) {
                    if (!(failure.getCause() instanceof AssertionError)) {
                        throw failure;
                    }

                    System.out.println(
                            "RED " + mutation[0] + ": " + failure.getCause().getMessage());
                    continue;
                }

                throw new AssertionError("Mutation survived: " + mutation[0]);
            }
        } finally {
            try (var paths = Files.walk(work)) {
                for (Path path : paths.sorted(Comparator.reverseOrder()).toList()) {
                    Files.delete(path);
                }
            }
        }
    }

    private static void execute(Path fixtures, Path helper, Path classes) throws Exception {
        Files.createDirectories(classes);
        var compiler = ToolProvider.getSystemJavaCompiler();

        if (compiler == null) {
            throw new IllegalStateException("A full JDK 25 or newer is required");
        }

        var arguments = new ArrayList<String>();
        arguments.add("--release");
        arguments.add("17");
        arguments.add("-d");
        arguments.add(classes.toString());

        try (var paths = Files.walk(fixtures)) {
            paths.filter(path -> path.toString().endsWith(".java"))
                    .sorted()
                    .forEach(path -> arguments.add(path.toString()));
        }

        arguments.add(helper.toString());

        if (compiler.run(null, System.out, System.err, arguments.toArray(String[]::new)) != 0) {
            throw new IOException("Fixture/helper compilation failed");
        }

        try (var loader =
                new URLClassLoader(
                        new java.net.URL[] {classes.toUri().toURL()},
                        ClassLoader.getPlatformClassLoader())) {
            loader.loadClass("Test")
                    .getMethod("main", String[].class)
                    .invoke(null, (Object) new String[0]);
        }
    }
}
