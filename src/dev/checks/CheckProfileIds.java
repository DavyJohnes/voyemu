// Checks that the code of src/ and the workflows of .github/ name no profile.
// Usage: java src/dev/checks/CheckProfileIds.java [project-root]
// The forbidden names are the directories of profiles/ except the common ones; the code is every
// C, C++, Objective-C++, Java and shell source of src/ outside third_party and tests, and every
// workflow of .github/. Behaviour follows profile.json properties, so the code names no profile.

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;
import java.util.stream.Stream;

// A single-file program started as java <file>.java; the launcher needs no package.
@SuppressWarnings("DefaultPackage")
public final class CheckProfileIds {
    private static final Set<String> SOURCE_SUFFIXES =
            Set.of(".c", ".cpp", ".h", ".hpp", ".mm", ".java", ".sh");
    private static final Set<String> WORKFLOW_SUFFIXES = Set.of(".yml", ".yaml");

    private final Path root;
    private final Set<String> names = new TreeSet<>();
    private final List<String> problems = new ArrayList<>();
    private int files;

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
        check.run();
        check.problems.forEach(System.out::println);
        System.out.println(
                "Profile id check: "
                        + check.names.size()
                        + " profiles, "
                        + check.files
                        + " files, "
                        + check.problems.size()
                        + " problems");
        System.exit(check.problems.isEmpty() ? 0 : 1);
    }

    private void run() throws IOException {
        Path profiles = root.resolve("profiles");

        if (Files.isDirectory(profiles)) {
            try (Stream<Path> list = Files.list(profiles)) {
                for (Path entry : list.toList()) {
                    String name = entry.getFileName().toString();

                    if (Files.isDirectory(entry) && !name.startsWith("common")) {
                        names.add(name);
                    }
                }
            }
        }

        scan(root.resolve("src"), SOURCE_SUFFIXES);
        scan(root.resolve(".github"), WORKFLOW_SUFFIXES);
    }

    private void scan(Path directory, Set<String> suffixes) throws IOException {
        if (!Files.isDirectory(directory)) {
            return;
        }

        try (Stream<Path> walk = Files.walk(directory)) {
            for (Path path : walk.filter(Files::isRegularFile).sorted().toList()) {
                String relative = root.relativize(path).toString().replace('\\', '/');

                if (selected(relative, suffixes)) {
                    check(path, relative);
                }
            }
        }
    }

    // Tests name real profiles as data; third-party code is not ours.
    private static boolean selected(String relative, Set<String> suffixes) {
        String file = relative.substring(relative.lastIndexOf('/') + 1);
        int dot = file.lastIndexOf('.');

        if (dot < 0 || !suffixes.contains(file.substring(dot))) {
            return false;
        }

        return !relative.contains("/third_party/")
                && !relative.startsWith("src/prepare/test/")
                && !relative.startsWith("src/dev/checks/test/")
                && !file.contains(".test.")
                && !file.endsWith("Test.java");
    }

    private void check(Path path, String relative) throws IOException {
        files++;
        List<String> lines = Files.readAllLines(path, StandardCharsets.UTF_8);

        for (int i = 0; i < lines.size(); i++) {
            for (String name : names) {
                if (lines.get(i).contains(name)) {
                    problems.add(relative + ":" + (i + 1) + ": names profile " + name);
                }
            }
        }
    }
}
