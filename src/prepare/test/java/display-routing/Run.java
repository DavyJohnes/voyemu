import static java.nio.file.LinkOption.NOFOLLOW_LINKS;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.TimeUnit;

/**
 * Host fixture for Free 7.3.1 routing. Run with java Run.java <hiboard|statusbar|rearscreen> ROOT
 * WORK JDK.
 */
class Run {
    public static void main(String[] args) throws Exception {
        if (args.length != 4 || !List.of("hiboard", "statusbar", "rearscreen").contains(args[0])) {
            throw new IllegalArgumentException(
                    "Arguments: <hiboard|statusbar|rearscreen> ROOT WORK JDK");
        }

        String variant = args[0];
        Path root = Path.of(args[1]).toRealPath();
        Path work = newWorkDirectory(root, Path.of(args[2]).toAbsolutePath());
        Path jdkBin = Path.of(args[3]).toRealPath().resolve("bin");
        String executableSuffix = System.getProperty("os.name").startsWith("Windows") ? ".exe" : "";

        String routerName =
                switch (variant) {
                    case "hiboard" -> "HiboardRouter";
                    case "statusbar" -> "StatusBarRouter";
                    default -> "RearScreen";
                };
        String patch =
                switch (variant) {
                    case "hiboard" -> "api30-hiboard-routing";
                    case "statusbar" -> "systemui-routing";
                    default -> "api30-thirdscreen-rear";
                };
        Path router =
                root.resolve("profiles/free-11-chinese-20260731-7.3.1/patches")
                        .resolve(patch)
                        .resolve("src/com/voyemu/output/" + routerName + ".java");

        Path classes = Files.createDirectory(work.resolve("classes"));
        var compile =
                new ArrayList<>(
                        List.of(
                                jdkBin.resolve("javac" + executableSuffix).toString(),
                                "--release",
                                "25",
                                "-d",
                                classes.toString()));
        Path fixtures = root.resolve("src/prepare/test/java/display-routing/fixtures");
        copyFixtures(fixtures.resolve("common"), work.resolve("fixtures"), compile);
        copyFixtures(fixtures.resolve(variant), work.resolve("fixtures"), compile);
        compile.add(router.toString());

        int javac = run(root, work, "javac", compile);
        int lifecycle =
                run(
                        root,
                        work,
                        "lifecycle",
                        List.of(
                                jdkBin.resolve("java" + executableSuffix).toString(),
                                "-cp",
                                classes.toString(),
                                "com.voyemu.output." + routerName + "HostTest"));
        String digest =
                HexFormat.of()
                        .formatHex(
                                MessageDigest.getInstance("SHA-256")
                                        .digest(Files.readAllBytes(router)));
        Files.writeString(
                work.resolve("result.json"),
                String.format(
                        Locale.ROOT,
                        """
                        {
                          "schema": 1,
                          "no_guest": true,
                          "router_sha256": "%s",
                          "tests": [
                            {
                              "name": "javac",
                              "exit": %d
                            },
                            {
                              "name": "lifecycle",
                              "exit": %d
                            }
                          ]
                        }
                        """,
                        digest,
                        javac,
                        lifecycle));
    }

    private static Path newWorkDirectory(Path root, Path requested) throws IOException {
        Path work = requested.getRoot();

        for (Path part : requested) {
            // Resolve existing components before handling ".." and missing descendants.
            Path next = work.resolve(part).normalize();
            work = Files.notExists(next, NOFOLLOW_LINKS) ? next : next.toRealPath();
        }

        Path jobs = root.resolve(".local/build/jobs");

        if (!work.startsWith(jobs) || work.equals(jobs) || !Files.notExists(work, NOFOLLOW_LINKS)) {
            throw new IllegalArgumentException(
                    "WORK must be a new directory inside " + jobs + ": " + requested);
        }

        Files.createDirectories(work.getParent());

        return Files.createDirectory(work);
    }

    private static void copyFixtures(Path sourceRoot, Path destination, List<String> compile)
            throws IOException {
        try (var sources = Files.walk(sourceRoot)) {
            for (Path source :
                    sources.filter(p -> p.toString().endsWith(".java")).sorted().toList()) {
                Path target = destination.resolve(sourceRoot.relativize(source));
                Files.createDirectories(target.getParent());
                Files.copy(source, target);
                compile.add(target.toString());
            }
        }
    }

    private static int run(Path root, Path work, String stage, List<String> command)
            throws IOException, InterruptedException {
        Path stdout = work.resolve(stage + ".stdout");
        Path stderr = work.resolve(stage + ".stderr");
        Process process =
                new ProcessBuilder(command)
                        .directory(root.toFile())
                        .redirectOutput(stdout.toFile())
                        .redirectError(stderr.toFile())
                        .start();

        try {
            if (!process.waitFor(20, TimeUnit.SECONDS)) {
                throw new IOException(stage + " timed out after 20 seconds");
            }

            int exit = process.exitValue();

            if (exit != 0) {
                throw new IOException(
                        stage + " exited with " + exit + "\n" + Files.readString(stderr));
            }

            Files.copy(stdout, System.out);
            System.out.println();

            return exit;
        } finally {
            if (process.isAlive()) {
                process.destroyForcibly().waitFor();
            }
        }
    }
}
