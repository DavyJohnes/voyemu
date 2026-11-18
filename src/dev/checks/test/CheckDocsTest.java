// Runs CheckDocs on synthetic documentation trees and checks its exit code and findings.
// Usage: java src/dev/checks/test/CheckDocsTest.java src/dev/checks/CheckDocs.java [work-directory]

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
public final class CheckDocsTest {
    private static Path checker;
    private static Path work;
    private static int cases;
    private static final List<String> FAILURES = new ArrayList<>();

    private static final String WHY_EN =
            """
            # Choice

            ## Decision

            A.

            ## Context

            B.

            ## Alternatives

            C.

            ## Argument

            D.

            ## Consequences

            See [index](../index.md#where-next).
            """;
    private static final String WHY_RU =
            """
            # Выбор

            ## Решение

            А.

            ## Контекст

            Б.

            ## Альтернативы

            В.

            ## Аргумент

            Г.

            ## Последствия

            См. [главную](../index.ru.md#куда-дальше).
            """;

    public static void main(String[] args) throws IOException, InterruptedException {
        if (args.length < 1 || args.length > 2) {
            System.err.println(
                    "Usage: java src/dev/checks/test/CheckDocsTest.java"
                            + " src/dev/checks/CheckDocs.java [work-directory]");
            System.exit(2);
        }

        checker = Path.of(args[0]).toAbsolutePath();

        if (args.length == 2) {
            Files.createDirectories(Path.of(args[1]));
        }

        work =
                args.length == 2
                        ? Files.createTempDirectory(Path.of(args[1]), "check-docs-")
                        : Files.createTempDirectory("check-docs-");

        try {
            run();
        } finally {
            delete(work);
        }

        FAILURES.forEach(System.out::println);
        System.out.println("CheckDocs test: " + cases + " cases, " + FAILURES.size() + " failed");
        System.exit(FAILURES.isEmpty() ? 0 : 1);
    }

    private static Map<String, String> valid() {
        Map<String, String> files = new LinkedHashMap<>();

        files.put(
                "docs/index.md",
                """
                # Project

                The cluster shows `dashboard` values.

                ## Where next

                - [Choice](why/choice.md)
                - [Glossary](GLOSSARY.md#terms)

                | A | B |
                | --- | --- |
                | 1 | 2 |

                ```sh
                run --flag
                ```
                """);
        files.put(
                "docs/index.ru.md",
                """
                # Проект

                Приборка показывает значения `кластер`.

                ## Куда дальше

                - [Выбор](why/choice.ru.md)
                - [Глоссарий](GLOSSARY.ru.md#термины)

                | А | Б |
                | --- | --- |
                | 1 | 2 |

                ```sh
                run --flag
                ```
                """);
        files.put("docs/why/choice.md", WHY_EN);
        files.put("docs/why/choice.ru.md", WHY_RU);
        files.put(
                "docs/GLOSSARY.md",
                """
                # Glossary

                ## Terms

                | Term | Meaning | Do not use |
                | --- | --- | --- |
                | cluster | QNX guest | dashboard, head unit, server¹ |
                | VM | — | — |
                """);
        files.put(
                "docs/GLOSSARY.ru.md",
                """
                # Глоссарий

                ## Термины

                | Термин | Значение | Не использовать |
                | --- | --- | --- |
                | приборка | Гость QNX | кластер, ГУ |

                ## Глаголы

                | Не использовать | Писать |
                | --- | --- |
                | деплоит | публикует |
                """);
        files.put(
                "README.md",
                """
                # Root

                English | [Русский](README.ru.md)

                See [docs](docs/index.md) and [license](LICENSE).
                """);
        files.put(
                "README.ru.md",
                """
                # Корень

                [English](README.md) | Русский

                См. [документацию](docs/index.ru.md) и [лицензию](LICENSE).
                """);
        files.put("LICENSE", "MIT\n");

        return files;
    }

    private static void run() throws IOException, InterruptedException {
        // The glossary is reached only through a link with an anchor.
        expect("valid tree", valid(), 0, "0 problems");

        Map<String, String> files = valid();
        files.remove("docs/why/choice.ru.md");
        expect(
                "missing Russian pair",
                files,
                1,
                "docs/why/choice.md: missing pair docs/why/choice.ru.md");

        files = valid();
        files.remove("README.md");
        expect("missing root English pair", files, 1, "README.ru.md: missing pair README.md");

        files = valid();
        files.put("docs/how/other.md", "# Other\n\nText.\n");
        files.put("docs/how/other.ru.md", "# Другое\n\nТекст.\n");
        expect(
                "orphan page",
                files,
                1,
                "docs/how/other.md: page not reachable by links from docs/index.md");
        expect(
                "orphan Russian page",
                files,
                1,
                "docs/how/other.ru.md: page not reachable by links from docs/index.ru.md");

        files.put(
                "docs/why/choice.md",
                WHY_EN.replace("See [index]", "See [other](../how/other.md) and [index]"));
        files.put(
                "docs/why/choice.ru.md",
                WHY_RU.replace("См. [главную]", "См. [другое](../how/other.ru.md) и [главную]"));
        expect("page reachable through another page", files, 0, "0 problems");

        files = valid();
        files.put(
                "docs/index.ru.md",
                files.get("docs/index.ru.md").replace("- [Выбор](why/choice.ru.md)\n", ""));
        expect(
                "Russian page linked only from English pages",
                files,
                1,
                "docs/why/choice.ru.md: page not reachable by links from docs/index.ru.md");

        files = valid();
        files.remove("docs/index.md");
        files.remove("docs/index.ru.md");
        expect("missing start page", files, 1, "docs/index.md: missing documentation start page");

        runReadme();

        files = valid();
        files.put(
                "docs/index.ru.md",
                files.get("docs/index.ru.md").replace("## Куда дальше", "### Куда дальше"));
        expect(
                "heading level differs",
                files,
                1,
                "docs/index.ru.md:5: structure differs from docs/index.md:5: heading level 3"
                        + " instead of heading level 2");

        files = valid();
        files.put(
                "docs/index.ru.md",
                files.get("docs/index.ru.md").replace("| 1 | 2 |\n", "| 1 | 2 |\n| 3 | 4 |\n"));
        expect("table rows differ", files, 1, "table with 4 rows instead of table with 3 rows");

        files = valid();
        files.put(
                "docs/index.ru.md",
                files.get("docs/index.ru.md").replace("run --flag", "run --флаг"));
        expect("code block differs", files, 1, "structure differs from docs/index.md");

        files = valid();
        files.put(
                "docs/index.md",
                files.get("docs/index.md").replace("why/choice.md", "why/missing.md"));
        expect("broken link", files, 1, "docs/index.md:7: broken link: why/missing.md");

        files = valid();
        files.put(
                "docs/index.md",
                files.get("docs/index.md").replace("GLOSSARY.md#terms", "GLOSSARY.md#nothing"));
        expect("missing anchor", files, 1, "docs/index.md:8: missing anchor: GLOSSARY.md#nothing");

        files = valid();
        files.put(
                "README.md",
                files.get("README.md")
                        .replace("[docs](docs/index.md)", "[docs](docs/index.md#missing)"));
        expect(
                "missing anchor from root page",
                files,
                1,
                "README.md:5: missing anchor: docs/index.md#missing");

        files = valid();
        files.put(
                "docs/index.ru.md",
                files.get("docs/index.ru.md").replace("why/choice.ru.md", "why/choice.md"));
        expect(
                "Russian page links to English page",
                files,
                1,
                "docs/index.ru.md:7: Russian page links to the other language: why/choice.md");

        files = valid();
        files.put(
                "docs/index.md",
                files.get("docs/index.md").replace("why/choice.md", "../../outside.md"));
        expect(
                "link outside the repository",
                files,
                1,
                "link leaves the repository: ../../outside.md");

        runLines();
    }

    private static void runReadme() throws IOException, InterruptedException {
        Map<String, String> files = valid();

        files.put("docs/README.md", "# Docs\n\nSee [index](index.md).\n");
        files.put("docs/README.ru.md", "# Документация\n\nСм. [главную](index.ru.md).\n");
        expect("directory note need not be linked", files, 0, "0 problems");

        files.put("docs/how/README.md", "# How\n\nText.\n");
        files.put("docs/how/README.ru.md", "# Как\n\nТекст.\n");
        expect(
                "nested README is a page",
                files,
                1,
                "docs/how/README.md: page not reachable by links from docs/index.md");
    }

    private static void runLines() throws IOException, InterruptedException {
        Map<String, String> files = valid();

        files.put(
                "docs/index.md",
                files.get("docs/index.md").replace("# Project\n", "# Project ✅\n"));
        expect("emoji", files, 1, "docs/index.md:1: emoji or pictograph U+2705");

        files = valid();
        files.put(
                "docs/index.md",
                files.get("docs/index.md").replace("The cluster shows", "The cluster\nshows"));
        expect(
                "wrapped paragraph",
                files,
                1,
                "docs/index.md:4: paragraph continues on a second line");

        files = valid();
        files.put(
                "docs/index.md",
                files.get("docs/index.md")
                        .replace(
                                "- [Choice](why/choice.md)\n",
                                "- [Choice](why/choice.md)\n  continued\n"));
        expect(
                "wrapped list item",
                files,
                1,
                "docs/index.md:8: paragraph continues on a second line");

        files = valid();
        files.put(
                "docs/index.md",
                files.get("docs/index.md").replace("run --flag\n", "first\nsecond\n"));
        files.put(
                "docs/index.ru.md",
                files.get("docs/index.ru.md").replace("run --flag\n", "first\nsecond\n"));
        expect("lines inside a code block", files, 0, "0 problems");

        files = valid();
        files.put("docs/index.md", files.get("docs/index.md").stripTrailing());
        expect("missing final newline", files, 1, "docs/index.md: missing final newline");

        files = valid();
        files.put("docs/index.md", files.get("docs/index.md") + "\n");
        expect("two final newlines", files, 1, "docs/index.md: more than one final newline");

        files = valid();
        files.put(
                "docs/index.md",
                files.get("docs/index.md").replace("# Project\n", "# Project  \n"));
        expect("trailing whitespace", files, 1, "docs/index.md:1: trailing whitespace");

        files = valid();
        files.put(
                "docs/index.md",
                files.get("docs/index.md").replace("# Project\n", "# Project\r\n"));
        expect("CRLF", files, 1, "docs/index.md: line endings are not LF");

        files = valid();
        files.put(
                "docs/index.md",
                files.get("docs/index.md").replace("The cluster shows", "The dashboards show"));
        expect(
                "forbidden English plural",
                files,
                1,
                "docs/index.md:3: forbidden glossary variant \"dashboard\"");

        files = valid();
        files.put(
                "docs/index.md",
                files.get("docs/index.md").replace("The cluster shows", "The Head  Unit shows"));
        expect("forbidden English phrase", files, 1, "forbidden glossary variant \"head unit\"");

        files = valid();
        files.put(
                "docs/index.md",
                files.get("docs/index.md").replace("The cluster shows", "The server shows"));
        expect("footnoted variant is left to review", files, 0, "0 problems");

        files = valid();
        files.put(
                "docs/index.ru.md",
                files.get("docs/index.ru.md")
                        .replace("Приборка показывает", "Кластером показываются"));
        expect(
                "forbidden Russian inflection",
                files,
                1,
                "docs/index.ru.md:3: forbidden glossary variant \"кластер\"");

        files = valid();
        files.put(
                "docs/index.ru.md",
                files.get("docs/index.ru.md").replace("Приборка показывает", "Сервис деплоит"));
        expect("forbidden Russian verb", files, 1, "forbidden glossary variant \"деплоит\"");

        files = valid();
        files.put(
                "docs/index.ru.md",
                files.get("docs/index.ru.md").replace("Приборка показывает", "Гусь показывает"));
        expect("abbreviation matches only as a word", files, 0, "0 problems");

        files = valid();
        files.put(
                "docs/index.ru.md",
                files.get("docs/index.ru.md").replace("Приборка показывает", "ГУ показывает"));
        expect("forbidden Russian abbreviation", files, 1, "forbidden glossary variant \"ГУ\"");

        files = valid();
        files.put(
                "docs/why/choice.md",
                WHY_EN.replace(
                        "## Context\n\nB.\n\n## Alternatives\n\nC.\n\n",
                        "## Alternatives\n\nC.\n\n## Context\n\nB.\n\n"));
        files.put(
                "docs/why/choice.ru.md",
                WHY_RU.replace(
                        "## Контекст\n\nБ.\n\n## Альтернативы\n\nВ.\n\n",
                        "## Альтернативы\n\nВ.\n\n## Контекст\n\nБ.\n\n"));
        expect(
                "decision record order",
                files,
                1,
                "docs/why/choice.md: decision record sections must be Decision → Context →"
                        + " Alternatives → Argument → Consequences");

        files = valid();
        files.put("docs/why/choice.md", WHY_EN.replace("## Argument\n\nD.\n\n", ""));
        files.put("docs/why/choice.ru.md", WHY_RU.replace("## Аргумент\n\nГ.\n\n", ""));
        expect(
                "decision record without a section",
                files,
                1,
                "docs/why/choice.ru.md: decision record sections must be Решение");

        files = valid();
        files.put(
                "docs/index.md",
                files.get("docs/index.md")
                        .replace(
                                "The cluster shows",
                                "See /home/user/x and plans/2026 on KLG in .local/build/jobs/a; the"
                                        + " cluster shows"));
        expect("internal markers", files, 1, "absolute home path /home/");
        expect("internal host", files, 1, "internal host name KLG");
        expect("plans reference", files, 1, "internal plans/ reference");
        expect("private job path", files, 1, "private job path .local/build/jobs");
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
                new ProcessBuilder(java, "-Xmx128m", checker.toString(), root.toString())
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
