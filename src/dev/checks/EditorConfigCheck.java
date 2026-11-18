// Checks tracked text files against config/config-editor.
// Usage: java src/dev/checks/EditorConfigCheck.java [project-root]
// Enforces end_of_line, insert_final_newline (exactly one final newline), trim_trailing_whitespace
// and indent_style = space (no tab in leading indentation). Files with NUL bytes are binary and
// skipped.

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.regex.Pattern;

// A single-file program started as java <file>.java; the launcher needs no package.
@SuppressWarnings("DefaultPackage")
public final class EditorConfigCheck {
    private record Section(Pattern pattern, Map<String, String> properties) {}

    public static void main(String[] args) throws IOException, InterruptedException {
        if (args.length > 1) {
            System.err.println("Usage: java src/dev/checks/EditorConfigCheck.java [project-root]");
            System.exit(2);
        }

        Path root = Path.of(args.length == 1 ? args[0] : ".").toRealPath();
        List<Section> sections =
                parse(
                        Files.readAllLines(
                                root.resolve("config/config-editor"), StandardCharsets.UTF_8));
        int checked = 0;
        int failed = 0;

        for (String file : trackedFiles(root)) {
            Path path = root.resolve(file);

            if (!Files.isRegularFile(path) || Files.isSymbolicLink(path)) {
                continue;
            }

            byte[] bytes = Files.readAllBytes(path);

            if (binary(bytes)) {
                continue;
            }

            checked++;
            List<String> problems = check(bytes, properties(sections, file));

            if (!problems.isEmpty()) {
                failed++;

                for (String problem : problems) {
                    System.out.println(file + ": " + problem);
                }
            }
        }

        System.out.println(
                "EditorConfig check: " + checked + " text files, " + failed + " with violations");
        System.exit(failed == 0 ? 0 : 1);
    }

    private static List<Section> parse(List<String> lines) {
        List<Section> sections = new ArrayList<>();
        Section current = null;

        for (String raw : lines) {
            String line = raw.strip();

            if (line.isEmpty() || line.startsWith("#") || line.startsWith(";")) {
                continue;
            }

            if (line.startsWith("[") && line.endsWith("]")) {
                current = new Section(glob(line.substring(1, line.length() - 1)), new HashMap<>());
                sections.add(current);
                continue;
            }

            int equals = line.indexOf('=');

            if (equals < 0) {
                throw new IllegalArgumentException("Malformed EditorConfig line: " + raw);
            }

            if (current != null) {
                current.properties()
                        .put(
                                line.substring(0, equals).strip().toLowerCase(Locale.ROOT),
                                line.substring(equals + 1).strip().toLowerCase(Locale.ROOT));
            }
        }

        return sections;
    }

    // EditorConfig glob relative to the project root; a glob without '/' matches a file name at any
    // depth.
    private static Pattern glob(String glob) {
        String body =
                glob.startsWith("/") ? glob.substring(1) : glob.contains("/") ? glob : "**/" + glob;
        StringBuilder regex = new StringBuilder();
        int braces = 0;

        for (int i = 0; i < body.length(); i++) {
            char c = body.charAt(i);

            if (c == '*') {
                if (i + 1 < body.length() && body.charAt(i + 1) == '*') {
                    i++;

                    if (i + 1 < body.length() && body.charAt(i + 1) == '/') {
                        i++;
                        regex.append("(?:.*/)?");
                    } else {
                        regex.append(".*");
                    }
                } else {
                    regex.append("[^/]*");
                }
            } else if (c == '?') {
                regex.append("[^/]");
            } else if (c == '{') {
                braces++;
                regex.append("(?:");
            } else if (c == '}' && braces > 0) {
                braces--;
                regex.append(')');
            } else if (c == ',' && braces > 0) {
                regex.append('|');
            } else if (c == '[') {
                int end = body.indexOf(']', i + 1);

                if (end < 0) {
                    regex.append("\\[");
                } else {
                    String set = body.substring(i + 1, end);
                    regex.append('[')
                            .append(set.startsWith("!") ? "^" + set.substring(1) : set)
                            .append(']');
                    i = end;
                }
            } else {
                regex.append(Pattern.quote(String.valueOf(c)));
            }
        }

        return Pattern.compile(regex.toString());
    }

    private static Map<String, String> properties(List<Section> sections, String file) {
        Map<String, String> result = new HashMap<>();

        for (Section section : sections) {
            if (section.pattern().matcher(file).matches()) {
                section.properties()
                        .forEach((key, value) -> {
                            if (value.equals("unset")) {
                                result.remove(key);
                            } else {
                                result.put(key, value);
                            }
                        });
            }
        }

        return result;
    }

    private static List<String> check(byte[] bytes, Map<String, String> properties) {
        List<String> problems = new ArrayList<>();
        String text = new String(bytes, StandardCharsets.ISO_8859_1);

        checkEndings(text, properties, problems);
        checkLines(text, properties, problems);

        return problems;
    }

    private static void checkEndings(
            String text, Map<String, String> properties, List<String> problems) {
        String eol = properties.get("end_of_line");

        if (eol != null) {
            boolean crlf = text.contains("\r\n");
            boolean bareCr = text.replace("\r\n", "").contains("\r");
            boolean bareLf = text.replace("\r\n", "").contains("\n");

            if ((eol.equals("lf") && (crlf || bareCr))
                    || (eol.equals("crlf") && (bareLf || bareCr))
                    || (eol.equals("cr") && (crlf || bareLf))) {
                problems.add("line endings are not " + eol);
            }
        }

        if ("true".equals(properties.get("insert_final_newline")) && !text.isEmpty()) {
            if (!text.endsWith("\n")) {
                problems.add("missing final newline");
            } else if (text.endsWith("\n\n") || text.endsWith("\r\n\r\n")) {
                problems.add("more than one final newline");
            }
        }
    }

    private static void checkLines(
            String text, Map<String, String> properties, List<String> problems) {
        boolean trim = "true".equals(properties.get("trim_trailing_whitespace"));
        boolean spaces = "space".equals(properties.get("indent_style"));
        String[] lines = text.split("\r?\n|\r", -1);
        int trailing = 0;
        int tabs = 0;
        int firstTrailing = 0;
        int firstTab = 0;

        for (int i = 0; i < lines.length; i++) {
            String line = lines[i];

            if (trim && !line.isEmpty() && (line.endsWith(" ") || line.endsWith("\t"))) {
                trailing++;
                firstTrailing = firstTrailing == 0 ? i + 1 : firstTrailing;
            }

            if (spaces && tabIndented(line)) {
                tabs++;
                firstTab = firstTab == 0 ? i + 1 : firstTab;
            }
        }

        if (trailing > 0) {
            problems.add(
                    trailing + " line(s) with trailing whitespace, first at line " + firstTrailing);
        }

        if (tabs > 0) {
            problems.add(tabs + " line(s) indented with tabs, first at line " + firstTab);
        }
    }

    private static boolean tabIndented(String line) {
        int indent = 0;

        while (indent < line.length()
                && (line.charAt(indent) == ' ' || line.charAt(indent) == '\t')) {
            indent++;
        }

        return line.substring(0, indent).indexOf('\t') >= 0;
    }

    private static boolean binary(byte[] bytes) {
        for (int i = 0; i < Math.min(bytes.length, 8000); i++) {
            if (bytes[i] == 0) {
                return true;
            }
        }

        return false;
    }

    private static List<String> trackedFiles(Path root) throws IOException, InterruptedException {
        Process process =
                new ProcessBuilder("git", "-C", root.toString(), "ls-files", "-z")
                        .redirectError(ProcessBuilder.Redirect.INHERIT)
                        .start();
        ByteArrayOutputStream output = new ByteArrayOutputStream();

        try (InputStream input = process.getInputStream()) {
            input.transferTo(output);
        }

        if (process.waitFor() != 0) {
            throw new IOException("git ls-files failed in " + root);
        }

        List<String> files = new ArrayList<>();

        for (String file : output.toString(StandardCharsets.UTF_8).split("\0", -1)) {
            if (!file.isEmpty()) {
                files.add(file);
            }
        }

        return files;
    }
}
