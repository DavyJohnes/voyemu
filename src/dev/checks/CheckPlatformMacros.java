// Checks that platform conditional compilation stays in the thin src/app/os/ backends: no C, C++ or
// Objective-C++ source elsewhere under src/ tests a host or architecture macro in #if, #ifdef,
// #ifndef or #elif. config/config-platform-macros.txt lists the files that still do, one
// "path reason" line each, as the progress counter of the move to os/ backends.
// Usage: java src/dev/checks/CheckPlatformMacros.java [project-root]
// Prints file:line for every hit outside the list and every listed file without hits; exits
// non-zero
// on either.

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Stream;

// A single-file program started as java <file>.java; the launcher needs no package.
@SuppressWarnings("DefaultPackage")
public final class CheckPlatformMacros {
    private static final String ALLOWLIST = "config/config-platform-macros.txt";
    private static final List<String> SCANNED = List.of("src");
    // The os/ backends own platform code; pinned third-party bytes and the formatter fixtures are
    // outside the rule.
    private static final List<String> EXCLUDED =
            List.of("src/app/os/", "src/app/third_party/", "src/dev/format/test/");
    private static final Pattern SOURCE = Pattern.compile(".*\\.(c|h|cpp|hpp|mm|inc)$");
    private static final Pattern CONDITIONAL =
            Pattern.compile("^\\s*#\\s*(if|ifdef|ifndef|elif|elifdef|elifndef)\\b(.*)$");
    private static final Pattern MACRO =
            Pattern.compile(
                    "\\b(_WIN32|_WIN64|__APPLE__|__MACH__|__linux__|__gnu_linux__|__unix__"
                            + "|__x86_64__|_M_X64|_M_AMD64|__i386__|_M_IX86|__aarch64__|__arm64__"
                            + "|_M_ARM64|TARGET_OS_\\w+)\\b");

    private final Path root;
    private final Map<String, String> allowed = new LinkedHashMap<>();
    private final Map<String, Integer> allowedHits = new LinkedHashMap<>();
    private final List<String> findings = new ArrayList<>();
    private int outside;

    private CheckPlatformMacros(Path root) {
        this.root = root;
    }

    public static void main(String[] args) throws IOException {
        if (args.length > 1) {
            System.err.println(
                    "Usage: java src/dev/checks/CheckPlatformMacros.java [project-root]");
            System.exit(2);
        }

        CheckPlatformMacros check =
                new CheckPlatformMacros(Path.of(args.length == 1 ? args[0] : ".").toRealPath());
        check.allowlist();
        check.scan();
        int stale = 0;

        for (String file : check.allowed.keySet()) {
            if (!check.allowedHits.containsKey(file)) {
                check.findings.add(file + ": listed in " + ALLOWLIST + " without platform macros");
                stale++;
            }
        }

        int hits = check.allowedHits.values().stream().mapToInt(Integer::intValue).sum();
        check.findings.forEach(System.out::println);
        System.out.println(
                "Platform macro check: "
                        + check.allowedHits.size()
                        + " listed files with "
                        + hits
                        + " hits, "
                        + check.outside
                        + " hits outside the list, "
                        + stale
                        + " stale entries");
        System.exit(check.findings.isEmpty() ? 0 : 1);
    }

    private void allowlist() throws IOException {
        Path file = root.resolve(ALLOWLIST);

        if (!Files.isRegularFile(file)) {
            return;
        }

        List<String> lines = Files.readAllLines(file, StandardCharsets.UTF_8);

        for (int i = 0; i < lines.size(); i++) {
            String line = lines.get(i).strip();

            if (line.isEmpty() || line.startsWith("#")) {
                continue;
            }

            String[] parts = line.split("\\s+", 2);

            if (parts.length < 2 || allowed.containsKey(parts[0])) {
                findings.add(
                        ALLOWLIST + ":" + (i + 1) + ": one \"path reason\" line per file expected");
                continue;
            }

            allowed.put(parts[0], parts[1]);
        }
    }

    private void scan() throws IOException {
        for (String top : SCANNED) {
            Path directory = root.resolve(top);

            if (!Files.isDirectory(directory)) {
                continue;
            }

            List<Path> files;

            try (Stream<Path> walk = Files.walk(directory)) {
                files = walk.filter(Files::isRegularFile).sorted().toList();
            }

            for (Path file : files) {
                String rel = root.relativize(file).toString().replace('\\', '/');

                if (!SOURCE.matcher(rel).matches() || EXCLUDED.stream().anyMatch(rel::startsWith)) {
                    continue;
                }

                scanText(rel, new String(Files.readAllBytes(file), StandardCharsets.UTF_8));
            }
        }
    }

    private void scanText(String rel, String text) {
        String[] lines = text.split("\n", -1);

        for (int i = 0; i < lines.length; i++) {
            Matcher conditional = CONDITIONAL.matcher(lines[i]);

            if (!conditional.matches()) {
                continue;
            }

            Matcher macro = MACRO.matcher(conditional.group(2));

            if (!macro.find()) {
                continue;
            }

            if (allowed.containsKey(rel)) {
                allowedHits.merge(rel, 1, Integer::sum);
            } else {
                findings.add(rel + ":" + (i + 1) + ": " + macro.group(1));
                outside++;
            }
        }
    }
}
