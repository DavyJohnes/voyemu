# Checks

Static checks run before every commit and in [CI](ci.md). Each uses a pinned tool, tool configurations live in `config/`, and each finding is fixed or suppressed with a comment that gives the reason.

| Check | Command | Tool and configuration |
| --- | --- | --- |
| Formatting and Checkstyle | `cmake --build .local/build/native --target format-check` | clang-format 23.1.3 with `config/config-clang-format.yaml`, Uncrustify 0.83.0 with `config/config-uncrustify.cfg`, `src/dev/format/JavaBlankLines.java`, google-java-format 2.0.0 in AOSP style, Checkstyle 12.3.1 with `config/config-checkstyle.xml` |
| clang-tidy | `cmake --build .local/build/native --target tidy-check` | clang-tidy 23.1.3 with `config/config-clang-tidy.yaml` |
| Error Prone | `cmake --build .local/build/native --target errorprone-check` | Error Prone 2.52.0 on the pinned JDK |
| EditorConfig | `java src/dev/checks/EditorConfigCheck.java .` | Line endings, final newline, trailing whitespace and indentation by `config/config-editor` |
| Documentation | `java src/dev/checks/CheckDocs.java .` | Page pairs, links, glossary terms and decision records |

## lint and lint-fix

`lint` runs every check of the table without changing files; [CI](ci.md) runs it. `lint-fix` is the local command before a commit: it applies every automatic fix, then runs `lint`, which fails on the findings that remain. Fix those by hand.

```sh
cmake --build .local/build/native --target lint-fix
cmake --build .local/build/native --target lint
```

`lint-fix` first runs `tidy-fix`, which applies the clang-tidy fix-its of the checks that only restate the same code: braces around statements, `else` after `return`, redundant control flow, `nullptr` and `override`. Fix-its of the `bugprone` checks change behavior, so they stay manual. Then `lint-fix` runs the `format` target, which also formats the clang-tidy fixes. `lint-fix` invokes `cmake --build` for its steps; `CMAKE_BUILD_PARALLEL_LEVEL` bounds their parallel jobs.

## Formatting and Checkstyle

`format-check` checks C, C++ and Objective-C++ sources in `src/app/`, `src/dev/` and `src/guest/` and Java sources in `src/prepare/`, `src/guest/` and `profiles/`, then runs Checkstyle on the Java sources. Blank lines separate control statements (`if`, `for`, `while`, `do`, `switch` and `synchronized`) from their neighbors. They follow a whole `try` statement with its handlers, and in Java also precede it; they also precede a `return` that is not first in its block and follow the declarations at the top of a function. Uncrustify sets them between two clang-format passes, and `JavaBlankLines.java` sets them in Java before google-java-format. One blank line also separates function and method definitions from each other and from the declarations and other code around them at file, namespace and class scope: a comment directly above a definition stays with it, consecutive declarations stay together, and a namespace or `extern "C"` body neither starts nor ends with a blank line. google-java-format separates Java methods the same way. Before it checks the tree, the target runs `src/dev/format/format.test.cmake` on the fixtures in `src/dev/format/test/`: check mode must reject each unformatted fixture, and fix mode must turn it into its `.formatted` copy, which check mode then accepts. `cmake -P src/dev/format/uncrustify.cmake` installs the pinned Uncrustify: it downloads the executable for the host that the [uncrustify workflow](ci.md#the-uncrustify-workflow) builds and checks its SHA-256. With `-DVOYEMU_UNCRUSTIFY_FROM_SOURCE=ON` it builds Uncrustify from the pinned release tarball with the pinned Zig instead and checks the result against the same pin; only this build needs Python 3. Pinned third-party code, generated files and the formatter fixtures are excluded. The `format` target rewrites the files instead of checking them. The script needs no build tree:

```sh
cmake -DVOYEMU_FORMAT_MODE=check -P src/dev/format/format.cmake
```

## clang-tidy

`tidy-check` first builds every checked target, so generated headers exist, then runs clang-tidy on every Voyemu C, C++ and Objective-C++ source of the build tree through `compile_commands.json`. Profile components and pinned third-party code are outside its scope. `VOYEMU_TIDY_JOBS` bounds the parallel processes, 4 by default. Run it from a tree of each target platform, since each compiles its own host backends.

## Error Prone

`errorprone-check` compiles the preparation tools and their tests, guest Java code and the tests that run guest Java code on JVM doubles of Android with Error Prone; every Error Prone or javac warning fails the check. It needs the pinned Java libraries, D8 and apksigner. The script also runs without a build tree:

```sh
cmake -P src/app/errorprone.cmake
```

## EditorConfig check

`EditorConfigCheck` checks every tracked text file against `config/config-editor`: the line ending, exactly one final newline, no trailing whitespace and spaces in leading indentation. Files with NUL bytes are binary and skipped.

## Documentation check

`CheckDocs` checks `docs/` and the root `README`, `CHANGELOG`, `SECURITY` and `CONTRIBUTING` pairs: every page has an English and a Russian file with the same heading, table and code-block structure. Every docs page is reachable by links from `docs/index.md`, and every Russian page from `docs/index.ru.md`; these links give the reading order in which the Voyemu website publishes the documentation. Links and anchors must resolve, decision records must follow their five sections, and prose must contain no forbidden glossary variants, emoji or internal markers. It exits with a non-zero code on any finding.

```sh
java src/dev/checks/CheckDocs.java
```

Tool paths default to the pins in `src/app/toolchain.json`; `VOYEMU_CLANG_FORMAT`, `VOYEMU_GOOGLE_JAVA_FORMAT` and `VOYEMU_CLANG_TIDY` select other binaries of the same version.
