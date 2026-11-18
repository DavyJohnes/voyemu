package com.voyemu.prepare;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.spi.ToolProvider;

/**
 * Standard installed JDK compiler; Java 8 guest output and explicit Android classpath. javac comes
 * through the java.base tool SPI, so the helper needs no java.compiler module on the Java runtime
 * of preparation, which has no javac.
 */
final class JavaSources {
    static void compile(List<Path> sources, Path output, Path androidJar, List<Path> libraries)
            throws Exception {
        Failure.require(
                !sources.isEmpty() && sources.size() <= 16384,
                "JAVA_SOURCE_MISSING",
                "Bounded Java source list required",
                5);
        Failure.require(
                Files.isRegularFile(androidJar) && !Files.isSymbolicLink(androidJar),
                "ANDROID_PLATFORM_MISSING",
                "Android platform JAR is missing",
                3);
        var compiler = ToolProvider.findFirst("javac");
        Failure.require(
                compiler.isPresent(),
                "JDK_REQUIRED",
                "Preparation requires a full JDK with javac",
                3);
        List<Path> classpath = new ArrayList<>();
        classpath.add(androidJar);

        for (Path library : libraries) {
            Failure.require(
                    Files.isRegularFile(library) && !Files.isSymbolicLink(library),
                    "JAVA_LIBRARY_INVALID",
                    "Ordinary compile dependency required",
                    5);
            classpath.add(library);
        }

        // Guest code stays at --release 8: it limits java.* to the Java 8 APIs of the API 28 guest
        // and D8 desugars its language level. Host code, host tests and their runners use 25.
        List<String> args =
                new ArrayList<>(
                        List.of(
                                "--release",
                                "8",
                                "-encoding",
                                "UTF-8",
                                "-proc:none",
                                "-nowarn",
                                "-classpath",
                                String.join(
                                        java.io.File.pathSeparator,
                                        classpath.stream().map(Path::toString).toList()),
                                "-d",
                                output.toString()));

        for (Path source : sources.stream().sorted().toList()) {
            Failure.require(
                    Files.isRegularFile(source)
                            && !Files.isSymbolicLink(source)
                            && source.toString().endsWith(".java"),
                    "JAVA_SOURCE_INVALID",
                    "Invalid Java source",
                    5);
            Storage.confined(source, ".");
            args.add(source.toString());
        }

        Storage.directory(output);
        StringWriter messages = new StringWriter();
        PrintWriter writer = new PrintWriter(messages);
        boolean success = compiler.get().run(writer, writer, args.toArray(String[]::new)) == 0;
        writer.flush();
        Failure.require(success, "JAVA_COMPILE_FAILED", messages.toString(), 5);

        try (var paths = Files.walk(output)) {
            Failure.require(
                    paths.anyMatch(path -> path.toString().endsWith(".class")),
                    "JAVA_OUTPUT_MISSING",
                    "Compiler produced no class files",
                    5);
        }
    }
}
