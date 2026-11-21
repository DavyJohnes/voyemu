// Runs CheckPlatformMacros on synthetic project trees and checks its exit code and findings.
// Usage: java src/dev/checks/test/CheckPlatformMacrosTest.java
// src/dev/checks/CheckPlatformMacros.java
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
public final class CheckPlatformMacrosTest {
    private static final String ALLOWLIST = "config/config-platform-macros.txt";
    private static Path checker;
    private static Path work;
    private static int cases;
    private static final List<String> FAILURES = new ArrayList<>();

    public static void main(String[] args) throws IOException, InterruptedException {
        if (args.length < 1 || args.length > 2) {
            System.err.println(
                    "Usage: java src/dev/checks/test/CheckPlatformMacrosTest.java"
                            + " src/dev/checks/CheckPlatformMacros.java [work-directory]");
            System.exit(2);
        }

        checker = Path.of(args[0]).toAbsolutePath();

        if (args.length == 2) {
            Files.createDirectories(Path.of(args[1]));
        }

        work =
                args.length == 2
                        ? Files.createTempDirectory(Path.of(args[1]), "check-platform-macros-")
                        : Files.createTempDirectory("check-platform-macros-");

        try {
            run();
        } finally {
            delete(work);
        }

        FAILURES.forEach(System.out::println);
        System.out.println(
                "CheckPlatformMacros test: " + cases + " cases, " + FAILURES.size() + " failed");
        System.exit(FAILURES.isEmpty() ? 0 : 1);
    }

    private static Map<String, String> valid() {
        Map<String, String> files = new LinkedHashMap<>();

        files.put("src/app/os/fs_windows.cpp", "#ifdef _WIN32\n#endif\n");
        files.put(
                "src/app/os/platform.hpp",
                "#if defined(__APPLE__) && defined(__arm64__)\n#endif\n");
        files.put("src/app/core/process.cpp", "// _WIN32 hosts use os/process.hpp.\nint a;\n");
        files.put("src/app/third_party/lib/lib.h", "#ifdef _WIN32\n#endif\n");
        files.put("src/dev/format/test/blank-lines.cpp", "#ifdef __APPLE__\n#endif\n");
        files.put("src/app/CMakeLists.txt", "if(WIN32) # _WIN32\nendif()\n");
        files.put("src/app/core/NOMINMAX.hpp", "#ifndef NOMINMAX\n#define NOMINMAX\n#endif\n");

        return files;
    }

    private static void run() throws IOException, InterruptedException {
        expect("valid tree", valid(), 0, "0 listed files with 0 hits, 0 hits outside the list");

        Map<String, String> files = valid();
        files.put("src/app/core/download.cpp", "int a;\n#ifdef _WIN32\n#endif\n");
        expect("ifdef outside os", files, 1, "src/app/core/download.cpp:2: _WIN32");

        files = valid();
        files.put("src/guest/qnx/a.test.cpp", "#if X\n#elif defined(__aarch64__)\n#endif\n");
        expect("elif architecture", files, 1, "src/guest/qnx/a.test.cpp:2: __aarch64__");

        files = valid();
        files.put("src/app/ui/window.mm", "  #  if !defined(__linux__)\n#endif\n");
        expect("indented negated condition", files, 1, "src/app/ui/window.mm:1: __linux__");

        files = valid();
        files.put("src/app/core/a.cpp", "#ifndef _WIN32\n#endif\n");
        files.put("src/app/core/b.hpp", "#if TARGET_OS_OSX\n#endif\n");
        expect("hit total", files, 1, "2 hits outside the list");

        files = valid();
        files.put("src/app/core/a.cpp", "#ifdef _WIN32\n#endif\n#ifdef __APPLE__\n#endif\n");
        files.put(ALLOWLIST, "# Comment.\n\nsrc/app/core/a.cpp group 3 core: primitives\n");
        expect("listed file", files, 0, "1 listed files with 2 hits, 0 hits outside the list");

        files = valid();
        files.put(ALLOWLIST, "src/app/core/process.cpp group 3 core\n");
        expect(
                "stale entry",
                files,
                1,
                "src/app/core/process.cpp: listed in " + ALLOWLIST + " without platform macros");

        files = valid();
        files.put("src/app/core/a.cpp", "#ifdef _WIN32\n#endif\n");
        files.put(ALLOWLIST, "src/app/core/a.cpp\n");
        expect("entry without reason", files, 1, ALLOWLIST + ":1: one \"path reason\" line");
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
