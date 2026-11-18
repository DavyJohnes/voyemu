package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Runs the actual startup audio provider with controlled Android boundary doubles. */
public final class FreeStartupAudioHostTest {
    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            throw new IllegalArgumentException("Expected ROOT WORK JDK");
        }

        Path root = Path.of(args[0]).toRealPath();
        Path work = HostTestRunner.createJob(root, Path.of(args[1]));
        Path jdk = Path.of(args[2]).toRealPath();
        Path classes = Files.createDirectory(work.resolve("classes"));

        Path production =
                root.resolve(
                        "profiles/free-11-chinese-20260731-7.3.1/components/wheel-input/src/com/voyemu/input");
        List<Path> sources =
                new ArrayList<>(
                        HostTestRunner.javaSources(
                                root.resolve("src/prepare/test/free-startup-audio")));
        sources.add(production.resolve("FreeKeyInputProvider.java"));
        sources.add(production.resolve("FreeKeyInputSession.java"));
        Path policy = production.resolve("FreeStartupAudioPolicy.java");

        if (Files.isRegularFile(policy)) {
            sources.add(policy);
        }

        sources.add(
                root.resolve(
                        "src/prepare/test/java/com/voyemu/input/FreeStartupAudioProviderTest.java"));

        Path java = HostTestRunner.jdkTool(jdk, "java");
        Path javac = HostTestRunner.jdkTool(jdk, "javac");
        var jdkPins = new LinkedHashMap<String, String>();

        for (Path tool : List.of(jdk.resolve("release"), java, javac)) {
            jdkPins.put(jdk.relativize(tool).toString().replace('\\', '/'), sha(tool));
        }

        var sourcePins = new LinkedHashMap<String, String>();

        for (Path source : sources) {
            sourcePins.put(root.relativize(source).toString().replace('\\', '/'), sha(source));
        }

        var compile =
                new ArrayList<>(
                        List.of(
                                javac.toString(),
                                "-J-Xmx256m",
                                "--release",
                                "25",
                                "-d",
                                classes.toString()));
        sources.forEach(path -> compile.add(path.toString()));
        var run =
                List.of(
                        java.toString(),
                        "-Xmx256m",
                        "-cp",
                        classes.toString(),
                        "com.voyemu.input.FreeStartupAudioProviderTest");
        var commands = new ArrayList<Map<String, Object>>();

        for (List<String> command : List.of(compile, run)) {
            Path log = work.resolve("command-" + (commands.size() + 1) + ".log");
            int exit =
                    HostTestRunner.run(
                            new ProcessBuilder(command)
                                    .redirectErrorStream(true)
                                    .redirectOutput(log.toFile()),
                            30);
            String output = Files.readString(log);
            System.out.print(output);
            commands.add(Map.of("argv", command, "exit", exit, "output", output));
            Files.writeString(
                    work.resolve("receipt.json"),
                    Json.write(
                                    Map.of(
                                            "jdk",
                                            jdk.toString(),
                                            "jdk_files",
                                            jdkPins,
                                            "sources",
                                            sourcePins,
                                            "commands",
                                            commands))
                            + "\n");

            if (exit != 0) {
                System.exit(exit);
            }
        }
    }

    private static String sha(Path path) throws Exception {
        return HexFormat.of()
                .formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(path)));
    }
}
