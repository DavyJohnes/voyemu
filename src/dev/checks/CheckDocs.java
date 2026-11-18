// Checks the documentation in docs/ and the root README, CHANGELOG, SECURITY and CONTRIBUTING
// pairs.
// Usage: java src/dev/checks/CheckDocs.java [project-root]
// Every page is a pair X.md/X.ru.md with the same heading, table and code-block structure;
// every docs page is reachable by links from docs/index.md or docs/index.ru.md, which give the
// reading order; local links and anchors resolve; no emoji, internal markers or forbidden glossary
// variants; one paragraph per line; exactly one final LF; why/* pages follow the five-section
// template.

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Stream;

// A single-file program started as java <file>.java; the launcher needs no package.
@SuppressWarnings("DefaultPackage")
public final class CheckDocs {
    private static final List<String> ROOT_PAGES =
            List.of("README", "CHANGELOG", "SECURITY", "CONTRIBUTING");
    private static final List<String> WHY_EN =
            List.of("Decision", "Context", "Alternatives", "Argument", "Consequences");
    private static final List<String> WHY_RU =
            List.of("Решение", "Контекст", "Альтернативы", "Аргумент", "Последствия");
    private static final Pattern FENCE = Pattern.compile("^ {0,3}(`{3,}|~{3,})");
    private static final Pattern HEADING = Pattern.compile("^(#{1,6})\\s+(.*?)\\s*#*\\s*$");
    private static final Pattern LIST = Pattern.compile("^\\s*(?:[-*+]|\\d+[.)])\\s+");
    private static final Pattern CODE_SPAN = Pattern.compile("(`+)(.+?)\\1");
    private static final Pattern LINK =
            Pattern.compile(
                    "\\[((?:[^\\[\\]]|\\[[^\\]]*\\])*)\\]\\(\\s*<?([^)\\s>]*)>?(?:\\s+\"[^\"]*\")?\\s*\\)");
    private static final Pattern SCHEME = Pattern.compile("^[A-Za-z][A-Za-z0-9+.-]*:");
    private static final Pattern URL = Pattern.compile("[A-Za-z][A-Za-z0-9+.-]*://\\S+");
    private static final Set<String> NOT_PUBLISHED = Set.of("README", "AGENTS", "CLAUDE");
    private static final Pattern VARIANT_SEPARATOR = Pattern.compile("[,/]");
    private static final Pattern WHITESPACE = Pattern.compile("\\s+");
    private static final String FOOTNOTE_MARKS = "¹²³⁴⁵⁶⁷⁸⁹⁰";
    private static final Map<Pattern, String> MARKERS = new LinkedHashMap<>();

    static {
        MARKERS.put(Pattern.compile("\\.local/build/jobs"), "private job path .local/build/jobs");
        MARKERS.put(
                Pattern.compile("(?<![A-Za-z0-9])KLG(?![A-Za-z0-9])"), "internal host name KLG");
        MARKERS.put(Pattern.compile("(?<![\\w.-])plans/"), "internal plans/ reference");
        MARKERS.put(Pattern.compile("/Users/"), "absolute home path /Users/");
        MARKERS.put(Pattern.compile("/home/"), "absolute home path /home/");
    }

    private enum Kind {
        BLANK,
        FENCE,
        CODE,
        HEADING,
        TABLE,
        LIST,
        QUOTE,
        HTML,
        TEXT
    }

    private record Line(int number, String text, Kind kind) {}

    private record Token(int line, String value) {}

    private record Heading(int line, int level, String text) {}

    private record Forbidden(String variant, Pattern pattern) {}

    private static final class Page {
        final String rel;
        final Path path;
        final boolean russian;
        final List<Line> lines = new ArrayList<>();
        final List<Token> structure = new ArrayList<>();
        final List<Heading> headings = new ArrayList<>();
        final Set<String> anchors = new LinkedHashSet<>();

        Page(String rel, Path path) {
            this.rel = rel;
            this.path = path;
            this.russian = rel.endsWith(".ru.md");
        }

        String id() {
            return rel.substring(0, rel.length() - (russian ? ".ru.md".length() : ".md".length()));
        }
    }

    private final Path root;
    private final Map<String, Page> pages = new TreeMap<>();
    private final List<String> problems = new ArrayList<>();

    private CheckDocs(Path root) {
        this.root = root;
    }

    public static void main(String[] args) throws IOException {
        if (args.length > 1) {
            System.err.println("Usage: java src/dev/checks/CheckDocs.java [project-root]");
            System.exit(2);
        }

        CheckDocs check = new CheckDocs(Path.of(args.length == 1 ? args[0] : ".").toRealPath());
        check.run();
        check.problems.forEach(System.out::println);
        System.out.println(
                "Docs check: "
                        + check.pages.size()
                        + " pages, "
                        + check.problems.size()
                        + " problems");
        System.exit(check.problems.isEmpty() ? 0 : 1);
    }

    private void run() throws IOException {
        collect();

        for (Page page : pages.values()) {
            checkPairing(page);
        }

        checkReachable();
        Map<Boolean, List<Forbidden>> glossary =
                Map.of(false, glossary("docs/GLOSSARY.md"), true, glossary("docs/GLOSSARY.ru.md"));

        for (Page page : pages.values()) {
            checkLines(
                    page,
                    page.id().equals("docs/GLOSSARY") ? List.of() : glossary.get(page.russian));
            checkLinks(page);

            if (page.id().startsWith("docs/why/") && !page.id().equals("docs/why/index")) {
                checkWhy(page);
            }

            // The glossary pair differs by design: only the Russian page has the calque rules.
            if (page.russian && !page.id().equals("docs/GLOSSARY")) {
                Page english = pages.get(page.id() + ".md");

                if (english != null) {
                    checkStructure(english, page);
                }
            }
        }
    }

    private void collect() throws IOException {
        List<Path> files = new ArrayList<>();
        Path docs = root.resolve("docs");

        if (Files.isDirectory(docs)) {
            try (Stream<Path> walk = Files.walk(docs)) {
                walk.filter(path ->
                                Files.isRegularFile(path)
                                        && path.getFileName().toString().endsWith(".md"))
                        .forEach(files::add);
            }
        }

        for (String name : ROOT_PAGES) {
            for (String suffix : List.of(".md", ".ru.md")) {
                Path path = root.resolve(name + suffix);

                if (Files.isRegularFile(path)) {
                    files.add(path);
                }
            }
        }

        for (Path path : files) {
            Page page = load(path);

            if (page != null) {
                pages.put(page.rel, page);
            }
        }
    }

    private String relative(Path path) {
        return root.relativize(path).toString().replace('\\', '/');
    }

    private Page load(Path path) throws IOException {
        Page page = new Page(relative(path), path);
        String text = decode(page, Files.readAllBytes(path));

        if (text == null) {
            return null;
        }

        String[] raw = text.split("\n", -1);
        int count = text.endsWith("\n") ? raw.length - 1 : raw.length;
        Scan scan = new Scan(page);

        for (int i = 0; i < count; i++) {
            String line = raw[i].replace("\r", "");
            int number = i + 1;

            if (!line.isEmpty() && Character.isWhitespace(line.charAt(line.length() - 1))) {
                report(page.rel, number, "trailing whitespace");
            }

            Kind kind = scan.kind(line, number);
            scan.table(kind, number);
            page.lines.add(new Line(number, line, kind));
        }

        scan.table(Kind.BLANK, 0);

        if (scan.fence != null) {
            report(page.rel, scan.codeStart, "unclosed code block");
        }

        page.structure.sort((a, b) -> Integer.compare(a.line(), b.line()));

        return page;
    }

    // Decodes strict UTF-8 and reports line ending problems; null when the bytes are not UTF-8.
    private String decode(Page page, byte[] bytes) {
        String text;

        try {
            text =
                    StandardCharsets.UTF_8
                            .newDecoder()
                            .onMalformedInput(CodingErrorAction.REPORT)
                            .onUnmappableCharacter(CodingErrorAction.REPORT)
                            .decode(ByteBuffer.wrap(bytes))
                            .toString();
        } catch (CharacterCodingException e) {
            report(page.rel, 0, "not valid UTF-8");

            return null;
        }

        if (text.indexOf('\r') >= 0) {
            report(page.rel, 0, "line endings are not LF");
        }

        if (!text.endsWith("\n")) {
            report(page.rel, 0, "missing final newline");
        } else if (text.endsWith("\n\n")) {
            report(page.rel, 0, "more than one final newline");
        }

        return text;
    }

    /** Line classification state of one page: open code fence, its text and the open table. */
    private static final class Scan {
        private final Page page;
        private final StringBuilder code = new StringBuilder();
        private final Map<String, Integer> slugs = new HashMap<>();
        private String fence;
        private int codeStart;
        private int tableStart;
        private int tableRows;

        Scan(Page page) {
            this.page = page;
        }

        Kind kind(String line, int number) {
            Matcher fenceMatch = FENCE.matcher(line);

            if (fence != null) {
                return insideFence(line, fenceMatch);
            }

            if (fenceMatch.find()) {
                fence = fenceMatch.group(1);
                code.setLength(0);
                codeStart = number;

                return Kind.FENCE;
            }

            if (line.isBlank()) {
                return Kind.BLANK;
            }

            Matcher heading = HEADING.matcher(line);

            if (heading.matches()) {
                heading(heading, number);

                return Kind.HEADING;
            }

            return block(line);
        }

        private Kind insideFence(String line, Matcher fenceMatch) {
            if (fenceMatch.find()
                    && fenceMatch.group(1).charAt(0) == fence.charAt(0)
                    && fenceMatch.group(1).length() >= fence.length()
                    && line.strip().equals(fenceMatch.group(1))) {
                page.structure.add(
                        new Token(
                                codeStart,
                                "code block " + Integer.toHexString(code.toString().hashCode())));
                fence = null;

                return Kind.FENCE;
            }

            code.append(line).append('\n');

            return Kind.CODE;
        }

        private void heading(Matcher heading, int number) {
            int level = heading.group(1).length();

            page.headings.add(new Heading(number, level, heading.group(2)));
            page.structure.add(new Token(number, "heading level " + level));
            String slug = slug(heading.group(2));
            int seen = slugs.merge(slug, 1, Integer::sum);
            page.anchors.add(seen == 1 ? slug : slug + "-" + (seen - 1));
        }

        private static Kind block(String line) {
            String stripped = line.stripLeading();

            if (stripped.startsWith("|")) {
                return Kind.TABLE;
            }

            if (LIST.matcher(line).find()) {
                return Kind.LIST;
            }

            if (stripped.startsWith(">")) {
                return Kind.QUOTE;
            }

            return stripped.startsWith("<") ? Kind.HTML : Kind.TEXT;
        }

        // Counts table rows; a non-table line closes the open table into one structure token.
        void table(Kind kind, int number) {
            if (kind == Kind.TABLE) {
                tableStart = tableRows == 0 ? number : tableStart;
                tableRows++;
            } else if (tableRows > 0) {
                page.structure.add(new Token(tableStart, "table with " + tableRows + " rows"));
                tableRows = 0;
            }
        }
    }

    // GitHub heading anchor: lowercase text without markup and punctuation, spaces become hyphens.
    private static String slug(String heading) {
        String text =
                LINK.matcher(heading).replaceAll("$1").replace("`", "").toLowerCase(Locale.ROOT);
        StringBuilder slug = new StringBuilder();

        text.codePoints()
                .forEach(c -> {
                    if (c == ' ') {
                        slug.append('-');
                    } else if (c == '-'
                            || c == '_'
                            || Character.isLetterOrDigit(c)
                            || Character.getType(c) == Character.NON_SPACING_MARK) {
                        slug.appendCodePoint(c);
                    }
                });

        return slug.toString();
    }

    private void checkPairing(Page page) {
        String counterpart = page.russian ? page.id() + ".md" : page.id() + ".ru.md";

        if (!pages.containsKey(counterpart) && !Files.isRegularFile(root.resolve(counterpart))) {
            report(page.rel, 0, "missing pair " + counterpart);
        }
    }

    // A docs page other than docs/README, docs/AGENTS or docs/CLAUDE, the notes for repository
    // browsing and agents.
    private static boolean published(Page page) {
        return page.rel.startsWith("docs/")
                && !NOT_PUBLISHED.contains(page.id().substring("docs/".length()));
    }

    // Every docs page is reachable by relative links from docs/index.md, or from docs/index.ru.md
    // for Russian pages.
    private void checkReachable() {
        for (String start : List.of("docs/index.md", "docs/index.ru.md")) {
            boolean russian = start.endsWith(".ru.md");
            Set<String> expected = new LinkedHashSet<>();

            for (Page page : pages.values()) {
                if (published(page) && page.russian == russian) {
                    expected.add(page.rel);
                }
            }

            if (expected.isEmpty()) {
                continue;
            }

            if (!pages.containsKey(start)) {
                report(start, 0, "missing documentation start page");
                continue;
            }

            Set<String> reached = new LinkedHashSet<>();
            List<String> queue = new ArrayList<>(List.of(start));

            while (!queue.isEmpty()) {
                String rel = queue.remove(queue.size() - 1);

                if (reached.add(rel)) {
                    linkedPages(pages.get(rel)).stream()
                            .filter(expected::contains)
                            .forEach(queue::add);
                }
            }

            for (String rel : expected) {
                if (!reached.contains(rel)) {
                    report(rel, 0, "page not reachable by links from " + start);
                }
            }
        }
    }

    // Repository-relative files of the local links on a page, without anchors.
    private List<String> linkedPages(Page page) {
        List<String> result = new ArrayList<>();

        for (Line line : page.lines) {
            if (line.kind() == Kind.CODE || line.kind() == Kind.FENCE) {
                continue;
            }

            Matcher matcher = LINK.matcher(CODE_SPAN.matcher(line.text()).replaceAll(" "));

            while (matcher.find()) {
                String file = matcher.group(2).replaceFirst("#.*", "");

                if (file.isEmpty() || SCHEME.matcher(file).find()) {
                    continue;
                }

                Path resolved = page.path.getParent().resolve(file).normalize();

                if (resolved.startsWith(root)) {
                    result.add(relative(resolved));
                }
            }
        }

        return result;
    }

    private List<Forbidden> glossary(String rel) throws IOException {
        List<Forbidden> result = new ArrayList<>();
        Path file = root.resolve(rel);

        if (!Files.isRegularFile(file)) {
            return result;
        }

        boolean russian = rel.endsWith(".ru.md");
        int column = -1;

        for (String line : Files.readAllLines(file, StandardCharsets.UTF_8)) {
            if (!line.stripLeading().startsWith("|")) {
                column = -1;
                continue;
            }

            List<String> cells = cells(line);

            if (column < 0) {
                column = -2;

                for (int i = 0; i < cells.size(); i++) {
                    String header = cells.get(i);

                    if (header.equals("Do not use") || header.equals("Не использовать")) {
                        column = i;
                    }
                }

                continue;
            }

            if (column < 0 || column >= cells.size() || cells.get(column).matches(":?-+:?")) {
                continue;
            }

            for (String raw : VARIANT_SEPARATOR.splitAsStream(cells.get(column)).toList()) {
                String variant = raw.strip().replaceAll("^[`«\"]+|[`»\"]+$", "");

                // A footnote mark documents a context-dependent exception; review enforces those
                // variants.
                if (variant.isEmpty()
                        || variant.equals("—")
                        || variant.chars().anyMatch(c -> FOOTNOTE_MARKS.indexOf(c) >= 0)) {
                    continue;
                }

                result.add(new Forbidden(variant, variantPattern(variant, russian)));
            }
        }

        return result;
    }

    // Whole words, case-insensitive. An uppercase abbreviation matches exactly; an English word
    // also matches
    // its plural; a Russian word matches its stem of four or more letters without final vowels,
    // plus an ending.
    private static Pattern variantPattern(String variant, boolean russian) {
        List<String> words = new ArrayList<>();

        for (String word : WHITESPACE.splitAsStream(variant).toList()) {
            if (word.length() > 1
                    && word.equals(word.toUpperCase(Locale.ROOT))
                    && !word.equals(word.toLowerCase(Locale.ROOT))) {
                words.add("(?-i:" + Pattern.quote(word) + ")" + (russian ? "" : "s?"));
            } else if (russian) {
                String stem = word.replaceAll("[аеёиоуыэюяйьАЕЁИОУЫЭЮЯЙЬ]+$", "");
                stem = stem.length() >= 4 ? stem : word;
                words.add(
                        Pattern.quote(stem)
                                + "\\p{L}{0,"
                                + (4 + word.length() - stem.length())
                                + "}");
            } else {
                words.add(Pattern.quote(word) + "(?:s|es)?");
            }
        }

        return Pattern.compile(
                "(?<![\\p{L}\\p{N}])" + String.join("\\s+", words) + "(?![\\p{L}\\p{N}])",
                Pattern.CASE_INSENSITIVE | Pattern.UNICODE_CASE);
    }

    private static List<String> cells(String line) {
        String body = line.strip();

        body =
                body.substring(
                        1,
                        body.endsWith("|") && body.length() > 1
                                ? body.length() - 1
                                : body.length());
        List<String> cells = new ArrayList<>();

        for (String cell : body.split("(?<!\\\\)\\|", -1)) {
            cells.add(cell.strip());
        }

        return cells;
    }

    // Prose without code spans, link targets and URLs.
    private static String prose(String line) {
        String text = CODE_SPAN.matcher(line).replaceAll(" ");

        text = LINK.matcher(text).replaceAll("$1");

        return URL.matcher(text).replaceAll(" ");
    }

    private void checkLines(Page page, List<Forbidden> forbidden) {
        Kind previous = Kind.BLANK;

        for (Line line : page.lines) {
            String text = line.text();

            for (Map.Entry<Pattern, String> marker : MARKERS.entrySet()) {
                if (marker.getKey().matcher(text).find()) {
                    report(page.rel, line.number(), marker.getValue());
                }
            }

            int pictograph = text.codePoints().filter(CheckDocs::pictograph).findFirst().orElse(-1);

            if (pictograph >= 0) {
                report(
                        page.rel,
                        line.number(),
                        String.format("emoji or pictograph U+%04X", pictograph));
            }

            if (line.kind() == Kind.TEXT
                    && (previous == Kind.TEXT || previous == Kind.LIST || previous == Kind.QUOTE)) {
                report(
                        page.rel,
                        line.number(),
                        "paragraph continues on a second line; keep one paragraph per line");
            }

            if (line.kind() != Kind.CODE && line.kind() != Kind.FENCE) {
                String prose = prose(text);

                for (Forbidden term : forbidden) {
                    if (term.pattern().matcher(prose).find()) {
                        report(
                                page.rel,
                                line.number(),
                                "forbidden glossary variant \"" + term.variant() + "\"");
                    }
                }
            }

            previous = line.kind();
        }
    }

    private static boolean pictograph(int c) {
        return (c >= 0x1F000 && c <= 0x1FAFF)
                || (c >= 0x2600 && c <= 0x27BF)
                || (c >= 0x2B50 && c <= 0x2B55)
                || c == 0x231A
                || c == 0x231B
                || (c >= 0x23E9 && c <= 0x23FA)
                || c == 0xFE0F
                || c == 0x200D
                || c == 0x20E3;
    }

    private void checkLinks(Page page) {
        for (Line line : page.lines) {
            if (line.kind() == Kind.CODE || line.kind() == Kind.FENCE) {
                continue;
            }

            Matcher matcher = LINK.matcher(CODE_SPAN.matcher(line.text()).replaceAll(" "));

            while (matcher.find()) {
                String target = matcher.group(2);

                if (!target.isEmpty() && !SCHEME.matcher(target).find()) {
                    checkLink(page, line.number(), target);
                }
            }
        }
    }

    private void checkLink(Page page, int number, String target) {
        int hash = target.indexOf('#');
        String file = hash < 0 ? target : target.substring(0, hash);
        String anchor = hash < 0 ? null : target.substring(hash + 1);
        Path resolved =
                file.isEmpty() ? page.path : page.path.getParent().resolve(file).normalize();

        if (!resolved.startsWith(root)) {
            report(page.rel, number, "link leaves the repository: " + target);

            return;
        }

        if (!Files.exists(resolved)) {
            report(page.rel, number, "broken link: " + target);

            return;
        }

        String rel = relative(resolved);
        Page linked = pages.get(rel);

        if (anchor != null && rel.endsWith(".md")) {
            Set<String> anchors = linked != null ? linked.anchors : anchors(resolved);

            if (!anchors.contains(anchor)) {
                report(page.rel, number, "missing anchor: " + target);
            }
        }

        if (linked != null && !linked.id().equals(page.id()) && linked.russian != page.russian) {
            report(
                    page.rel,
                    number,
                    (page.russian ? "Russian" : "English")
                            + " page links to the other language: "
                            + target);
        }
    }

    private Set<String> anchors(Path file) {
        try {
            Page page = load(file);

            return page == null ? Set.of() : page.anchors;
        } catch (IOException e) {
            return Set.of();
        }
    }

    private void checkWhy(Page page) {
        List<String> expected = page.russian ? WHY_RU : WHY_EN;
        List<String> sections = new ArrayList<>();
        int titles = 0;

        for (Heading heading : page.headings) {
            if (heading.level() == 1) {
                titles++;
            } else if (heading.level() == 2) {
                sections.add(heading.text());
            }
        }

        if (titles != 1) {
            report(page.rel, 0, "decision record needs exactly one title");
        }

        if (!sections.equals(expected)) {
            report(
                    page.rel,
                    0,
                    "decision record sections must be "
                            + String.join(" → ", expected)
                            + ", found "
                            + String.join(" → ", sections));
        }
    }

    private void checkStructure(Page english, Page russian) {
        int size = Math.min(english.structure.size(), russian.structure.size());

        for (int i = 0; i < size; i++) {
            Token en = english.structure.get(i);
            Token ru = russian.structure.get(i);

            if (!en.value().equals(ru.value())) {
                report(
                        russian.rel,
                        ru.line(),
                        "structure differs from "
                                + english.rel
                                + ":"
                                + en.line()
                                + ": "
                                + ru.value()
                                + " instead of "
                                + en.value());

                return;
            }
        }

        if (english.structure.size() != russian.structure.size()) {
            boolean extra = russian.structure.size() > size;
            List<Token> longer = extra ? russian.structure : english.structure;
            report(
                    russian.rel,
                    extra ? longer.get(size).line() : 0,
                    "structure differs from "
                            + english.rel
                            + ": "
                            + (extra ? "extra " : "missing ")
                            + longer.get(size).value());
        }
    }

    private void report(String rel, int line, String message) {
        problems.add(rel + (line > 0 ? ":" + line : "") + ": " + message);
    }
}
