// Checks that src/ and .github/ name no concrete profile: behavior follows profile.json properties.
// Usage: java src/dev/checks/CheckProfileIds.java [project-root]
// The forbidden strings come from the catalog: every directory of profiles/ except common*, its
// version tail after the date (1.2.3 of free-09-chinese-20250101-1.2.3) and its vehicle prefix in
// a startsWith or starts_with call. Prints file:line for every line with a hit and exits non-zero.

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
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
public final class CheckProfileIds {
    private static final List<String> SCANNED = List.of("src", ".github");
    // Pinned third-party bytes are outside the rule.
    private static final List<String> EXCLUDED = List.of("src/app/third_party/");
    private static final String RAM64 =
            "the Passion RAM64 binding is a guest contract; its version names the OEM layout";
    // Lines of a file that contain the text may name a profile; each entry gives its reason.
    private static final List<Allowed> ALLOWED =
            List.of(
                    new Allowed("src/guest/qnx/passion_ram64.h", "-system-ram64-v1", RAM64),
                    new Allowed("src/guest/qnx/passion_ram64.test.cpp", "-system-ram64-v", RAM64),
                    new Allowed("src/guest/qnx/qnx-passion-runtime.sh", "-system-ram64-v1", RAM64));
    private static final Pattern PROFILE =
            Pattern.compile("^([a-z]+)-[0-9]{2}-[a-z]+-[0-9]{8}-([A-Za-z0-9.-]+)$");

    private record Allowed(String file, String text, String reason) {}

    private final Path root;
    private final Map<String, Pattern> forbidden = new LinkedHashMap<>();
    private final List<String> hits = new ArrayList<>();

    private CheckProfileIds(Path root) {
        this.root = root;
    }

    public static void main(String[] args) throws IOException {
        if (args.length > 1) {
            System.err.println("Usage: java src/dev/checks/CheckProfileIds.java [project-root]");
            System.exit(2);
        }

        CheckProfileIds check =
                new CheckProfileIds(Path.of(args.length == 1 ? args[0] : ".").toRealPath());
        check.catalog();
        check.scan();
        check.hits.forEach(System.out::println);
        System.out.println(
                "Profile id check: "
                        + check.forbidden.size()
                        + " forbidden strings, "
                        + check.hits.size()
                        + " hits");
        System.exit(check.hits.isEmpty() ? 0 : 1);
    }

    private void catalog() throws IOException {
        Path profiles = root.resolve("profiles");

        if (!Files.isDirectory(profiles)) {
            return;
        }

        List<String> prefixes = new ArrayList<>();

        try (Stream<Path> list = Files.list(profiles)) {
            for (Path directory : list.sorted().toList()) {
                String name = directory.getFileName().toString();

                if (!Files.isDirectory(directory) || name.startsWith("common")) {
                    continue;
                }

                forbidden.put(name, Pattern.compile(Pattern.quote(name)));
                Matcher parts = PROFILE.matcher(name);

                if (!parts.matches()) {
                    continue;
                }

                // A tail stands alone: 1.2.3 is not part of 11.2.3 or 1.2.30.
                forbidden.put(
                        parts.group(2),
                        Pattern.compile(
                                "(?<![\\w.])" + Pattern.quote(parts.group(2)) + "(?!\\w|\\.\\w)"));

                if (!prefixes.contains(parts.group(1))) {
                    prefixes.add(parts.group(1));
                }
            }
        }

        for (String prefix : prefixes) {
            forbidden.put(
                    "starts_with(\"" + prefix,
                    Pattern.compile("(?:startsWith|starts_with)\\(\\s*\"" + Pattern.quote(prefix)));
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

                if (EXCLUDED.stream().anyMatch(rel::startsWith)) {
                    continue;
                }

                String text = decode(Files.readAllBytes(file));

                if (text != null) {
                    scanText(rel, text);
                }
            }
        }
    }

    private void scanText(String rel, String text) {
        String[] lines = text.split("\n", -1);

        for (int i = 0; i < lines.length; i++) {
            String line = lines[i];

            if (ALLOWED.stream()
                    .anyMatch(allowed ->
                            allowed.file().equals(rel) && line.contains(allowed.text()))) {
                continue;
            }

            List<String> found = new ArrayList<>();

            for (Map.Entry<String, Pattern> entry : forbidden.entrySet()) {
                if (entry.getValue().matcher(line).find()) {
                    found.add(entry.getKey());
                }
            }

            if (!found.isEmpty()) {
                hits.add(rel + ":" + (i + 1) + ": " + String.join(", ", found));
            }
        }
    }

    // Strict UTF-8 text without NUL bytes; null for binary files.
    private static String decode(byte[] bytes) {
        for (byte value : bytes) {
            if (value == 0) {
                return null;
            }
        }

        try {
            return StandardCharsets.UTF_8
                    .newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes))
                    .toString();
        } catch (CharacterCodingException e) {
            return null;
        }
    }
}
