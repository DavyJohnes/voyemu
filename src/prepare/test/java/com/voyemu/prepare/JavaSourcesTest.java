package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

public final class JavaSourcesTest {
    public static void main(String[] args) throws Exception {
        Path platform = Path.of(args[0]);
        Path project =
                Path.of(System.getProperty("voyemu.project.root", System.getProperty("user.dir")))
                        .toRealPath();

        if (!Files.isDirectory(project.resolve("src/prepare"))) {
            throw new AssertionError("Voyemu project root required");
        }

        Path parent = project.resolve(".local/build/tmp");
        Storage.directory(parent);
        Path root = Files.createTempDirectory(parent, "voyemu-java-compiler-test-").toRealPath();

        try {
            Path source = root.resolve("Own.java");
            Files.writeString(
                    source,
                    "package com.voyemu.own; public final class Own { public String"
                        + " text(android.os.Parcel p) { Runnable action = () -> { p.readString();"
                        + " }; action.run(); return p.readString(); } }\n");
            JavaSources.compile(List.of(source), root.resolve("classes"), platform, List.of());
            byte[] bytes = Files.readAllBytes(root.resolve("classes/com/voyemu/own/Own.class"));

            if (bytes[6] != 0 || bytes[7] != 52) {
                throw new AssertionError("Not Java 8 bytecode");
            }

            Files.writeString(source, "public class Own { syntax error }\n");

            try {
                JavaSources.compile(List.of(source), root.resolve("broken"), platform, List.of());
                throw new AssertionError("Broken source accepted");
            } catch (Failure expected) {
                if (!expected.code.equals("JAVA_COMPILE_FAILED")) {
                    throw expected;
                }
            }
        } finally {
            try (var paths = Files.walk(root)) {
                for (Path path : paths.sorted(java.util.Comparator.reverseOrder()).toList()) {
                    Files.delete(path);
                }
            }
        }

        System.out.println(
                "Standard JDK Java8 lambda/Android classpath and invalid-source guards passed");
    }
}
