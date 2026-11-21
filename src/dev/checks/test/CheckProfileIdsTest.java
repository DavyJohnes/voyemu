// Runs CheckProfileIds on synthetic project trees and checks its exit code and findings.
// Usage: java src/dev/checks/test/CheckProfileIdsTest.java src/dev/checks/CheckProfileIds.java
// [work-directory]

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Stream;

// A single-file program started as java <file>.java; the launcher needs no package.
@SuppressWarnings("DefaultPackage")
public final class CheckProfileIdsTest {
    private static final String PROFILE = "free-11-sample-20990101-1.2.3";
    private static Path checker;
    private static Path work;
    private static int cases;
    private static final List<String> FAILURES = new ArrayList<>();

    public static void main(String[] args) throws IOException, InterruptedException {
        if (args.length < 1 || args.length > 2) {
            System.err.println(
                    "Usage: java src/dev/checks/test/CheckProfileIdsTest.java"
                            + " src/dev/checks/CheckProfileIds.java [work-directory]");
            System.exit(2);
        }

        checker = Path.of(args[0]).toAbsolutePath();

        if (args.length == 2) {
            Files.createDirectories(Path.of(args[1]));
        }

        work =
                args.length == 2
                        ? Files.createTempDirectory(Path.of(args[1]), "check-profile-ids-")
                        : Files.createTempDirectory("check-profile-ids-");

        try {
            run();
        } finally {
            delete(work);
        }

        FAILURES.forEach(System.out::println);
        System.out.println(
                "CheckProfileIds test: " + cases + " cases, " + FAILURES.size() + " failed");
        System.exit(FAILURES.isEmpty() ? 0 : 1);
    }

    private static Map<String, String> valid() {
        Map<String, String> files = new LinkedHashMap<>();

        files.put("profiles/" + PROFILE + "/profile.json", "{\"platform\": \"qinggan11\"}\n");
        files.put("profiles/common-sample/components/README.md", "Shared.\n");
        files.put("src/app/profiles/select.cpp", "bool qinggan11 = platform == \"qinggan11\";\n");
        files.put(".github/workflows/ci.yml", "name: CI\n");

        return files;
    }

    private static void run() throws IOException, InterruptedException {
        expect("valid tree", valid(), 0, "0 problems");

        Map<String, String> files = valid();
        files.put(
                "src/app/profiles/named.hpp",
                "// header\nconstexpr auto id = \"" + PROFILE + "\";\n");
        expect(
                "C++ header names a profile",
                files,
                1,
                "src/app/profiles/named.hpp:2: names profile " + PROFILE);

        files = valid();
        files.put("src/prepare/java/com/example/Guard.java", "String p = \"" + PROFILE + "\";\n");
        expect(
                "Java source names a profile",
                files,
                1,
                "src/prepare/java/com/example/Guard.java:1: names profile " + PROFILE);

        files = valid();
        files.put("src/dev/ci/select.sh", "profile=" + PROFILE + "\n");
        expect("shell script names a profile", files, 1, "src/dev/ci/select.sh:1: names profile");

        files = valid();
        files.put(".github/workflows/ci.yml", "run: voyemu build " + PROFILE + "\n");
        expect(
                "workflow names a profile",
                files,
                1,
                ".github/workflows/ci.yml:1: names profile " + PROFILE);

        files = valid();
        files.put("src/app/profiles/select.test.cpp", "auto id = \"" + PROFILE + "\";\n");
        files.put("src/dev/release/archive.test.sh", "profile=" + PROFILE + "\n");
        files.put("src/guest/android/api/SelectTest.java", "String p = \"" + PROFILE + "\";\n");
        files.put("src/prepare/test/java/com/example/Fixture.java", "\"" + PROFILE + "\"\n");
        files.put("src/dev/checks/test/Fixture.java", "\"" + PROFILE + "\"\n");
        expect("tests name profiles as data", files, 0, "0 problems");

        files = valid();
        files.put("src/app/third_party/library/code.c", "/* " + PROFILE + " */\n");
        files.put("src/app/toolchain.json", "{\"" + PROFILE + "/component\": {}}\n");
        files.put("src/app/errorprone.cmake", "profiles/" + PROFILE + "/components\n");
        expect("third-party code and data files are not checked", files, 0, "0 problems");

        files = valid();
        files.put("src/app/profiles/shared.cpp", "auto path = \"profiles/common-sample\";\n");
        expect("common profile directories are not profiles", files, 0, "0 problems");
    }

    private static void expect(String name, Map<String, String> files, int exit, String output)
            throws IOException, InterruptedException {
        cases++;
        Path root = work.resolve("case-" + cases);

        for (Map.Entry<String, String> file : files.entrySet()) {
            Path path = root.resolve(file.getKey());
            Files.createDirectories(path.getParent());
            Files.writeString(path, file.getValue(), StandardCharsets.UTF_8);
        }

        String java = ProcessHandle.current().info().command().orElse("java");
        Process process =
                new ProcessBuilder(
                                java,
                                "-Xmx128m",
                                "-Dstdout.encoding=UTF-8",
                                checker.toString(),
                                root.toString())
                        .redirectErrorStream(true)
                        .start();
        String result = new String(process.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
        int code = process.waitFor();

        if (code != exit || !result.contains(output)) {
            FAILURES.add(
                    "FAIL "
                            + name
                            + ": expected exit "
                            + exit
                            + " with \""
                            + output
                            + "\", got exit "
                            + code
                            + ":\n"
                            + result);
        }
    }

    private static void delete(Path path) throws IOException {
        try (Stream<Path> walk = Files.walk(path)) {
            for (Path entry : walk.sorted(Comparator.reverseOrder()).toList()) {
                Files.delete(entry);
            }
        }
    }
}
