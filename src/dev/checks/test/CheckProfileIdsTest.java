// Runs CheckProfileIds on synthetic project trees and checks its exit code and findings.
// Usage: java src/dev/checks/test/CheckProfileIdsTest.java src/dev/checks/CheckProfileIds.java
//        [work-directory]

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

        files.put("profiles/free-09-chinese-20250101-1.2.3/profile.json", "{}\n");
        files.put("profiles/test-11-russian-20250202-nxp-rc9/profile.json", "{}\n");
        files.put("profiles/common/note.txt", "free-09-chinese-20250101-1.2.3\n");
        files.put("profiles/common-qinggan-09/note.txt", "shared\n");
        files.put("src/app/main.cpp", "// Version 11.2.3 and 1.2.30 are other versions.\n");
        files.put(".github/workflows/ci.yml", "name: ci\n");

        return files;
    }

    private static void run() throws IOException, InterruptedException {
        expect("valid tree", valid(), 0, "6 forbidden strings, 0 hits");

        Map<String, String> files = valid();
        files.put(
                "src/app/fetch.cpp", "int a;\nif (id == \"free-09-chinese-20250101-1.2.3\") {}\n");
        expect("full id", files, 1, "src/app/fetch.cpp:2: free-09-chinese-20250101-1.2.3, 1.2.3");

        files = valid();
        files.put(".github/workflows/ci.yml", "run: build test-11-russian-20250202-nxp-rc9\n");
        expect(
                "full id in .github",
                files,
                1,
                ".github/workflows/ci.yml:1: test-11-russian-20250202-nxp-rc9, nxp-rc9");

        files = valid();
        files.put("src/guest/note.h", "/* The 1.2.3 engine. */\n");
        expect("version tail", files, 1, "src/guest/note.h:1: 1.2.3\n");

        files = valid();
        files.put("src/prepare/Guard.java", "if (id.startsWith(\"test-\")) {}\n");
        expect("vehicle prefix", files, 1, "src/prepare/Guard.java:1: starts_with(\"test");

        files = valid();
        files.put("src/app/third_party/lib/version.h", "#define V \"1.2.3\"\n");
        expect("third-party code skipped", files, 0, "0 hits");

        files = valid();
        files.put("src/guest/qnx/passion_ram64.h", "\"test-1.2.3-system-ram64-v1\"\n");
        expect("allowed guest contract line", files, 0, "0 hits");
        files.put("src/guest/qnx/passion_ram64.h", "\"test-1.2.3-system-ram64-v1\"\n1.2.3\n");
        expect("other line of an allowed file", files, 1, "src/guest/qnx/passion_ram64.h:2: 1.2.3");

        files = valid();
        files.put("profiles/common-qinggan-11/profile.json", "{}\n");
        expect("common directories add nothing", files, 0, "6 forbidden strings, 0 hits");

        files = valid();
        files.put("src/app/a.cpp", "1.2.3\n");
        files.put("src/app/b.cpp", "x\nnxp-rc9\n");
        expect("hit total", files, 1, "6 forbidden strings, 2 hits");
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
