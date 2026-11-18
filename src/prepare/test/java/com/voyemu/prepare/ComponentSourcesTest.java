package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;

public final class ComponentSourcesTest {
    interface Checked {
        void run() throws Exception;
    }

    static int negatives;

    static void rejected(Checked work) throws Exception {
        try {
            work.run();
        } catch (Failure expected) {
            negatives++;

            return;
        }

        throw new AssertionError("Invalid shared component source accepted");
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]).toAbsolutePath();

        Files.createDirectories(root);
        byte[] source =
                "package own; final class Adapter {}\n"
                        .getBytes(java.nio.charset.StandardCharsets.UTF_8);
        String resource = "voyemu/profiles/common/components/bluetooth/src/Adapter.java";
        ComponentSources.copy(
                Map.of("src/Adapter.java", resource),
                root.resolve("success"),
                name -> {
                    if (!name.equals(resource)) {
                        throw new AssertionError("Unexpected resource");
                    }

                    return source;
                });

        if (!java.util.Arrays.equals(
                source, Files.readAllBytes(root.resolve("success/src/Adapter.java")))) {
            throw new AssertionError("Source changed");
        }

        // A native component takes its C source from the common components the same way.
        String nativeSource = "voyemu/profiles/common/components/qg-runtime/src/qg_runtime.c";
        ComponentSources.copy(
                Map.of("src/qg_runtime.c", nativeSource), root.resolve("native"), name -> source);

        if (!java.util.Arrays.equals(
                source, Files.readAllBytes(root.resolve("native/src/qg_runtime.c")))) {
            throw new AssertionError("Native source changed");
        }

        rejected(() ->
                ComponentSources.copy(
                        Map.of("src/qg_runtime.h", nativeSource),
                        root.resolve("header"),
                        name -> source));
        rejected(() ->
                ComponentSources.copy(
                        Map.of("src/Adapter.java", resource),
                        root.resolve("success"),
                        name -> source));
        rejected(() ->
                ComponentSources.copy(
                        Map.of("../Adapter.java", resource),
                        root.resolve("traversal"),
                        name -> source));
        rejected(() ->
                ComponentSources.copy(
                        Map.of("src/../Adapter.java", resource),
                        root.resolve("traversal2"),
                        name -> source));
        rejected(() ->
                ComponentSources.copy(
                        Map.of("src/a.jar", resource), root.resolve("binary"), name -> source));
        rejected(() ->
                ComponentSources.copy(
                        Map.of("src/Adapter.java", "voyemu/profiles/common/../secret.java"),
                        root.resolve("escape"),
                        name -> source));
        rejected(() ->
                ComponentSources.copy(
                        Map.of("src/Adapter.java", Map.of("resource", resource)),
                        root.resolve("object"),
                        name -> source));
        rejected(() -> ComponentSources.copy(Map.of(), root.resolve("empty"), name -> source));
        Path linked = root.resolve("linked");
        Files.createDirectories(linked);
        Files.createSymbolicLink(linked.resolve("src"), root.resolve("success/src"));
        rejected(() ->
                ComponentSources.copy(
                        Map.of("src/Adapter.java", resource), linked, name -> source));

        if (negatives != 9) {
            throw new AssertionError("Negative cases missing");
        }

        System.out.println(
                "ComponentSources: shared Java and C copies and 9 negative cases passed");
    }
}
