package com.voyemu.prepare;

import static com.voyemu.prepare.HostTestRunner.javaSources;
import static com.voyemu.prepare.HostTestRunner.jdkTool;
import static com.voyemu.prepare.HostTestRunner.pathOptions;
import static com.voyemu.prepare.HostTestRunner.require;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/** Compiles and executes the production guest sources with the guest Java 8 language contract. */
public final class PassionPowerHelperHostTest {
    public static void main(String[] args) throws Exception {
        var paths = pathOptions(args, "--root", "--work", "--jdk", "--platform");
        Path root = paths.get("--root").toRealPath();
        Path work = paths.get("--work");
        Path jdk = paths.get("--jdk");
        Path platform = paths.get("--platform").toRealPath();

        Files.createDirectories(work.getParent());
        Files.createDirectory(work);
        Path classes = work.resolve("classes");
        var compile =
                new ArrayList<>(
                        List.of(
                                jdkTool(jdk, "javac").toString(),
                                "--release",
                                "8",
                                "-encoding",
                                "UTF-8",
                                "-proc:none",
                                "-cp",
                                platform.toString(),
                                "-d",
                                classes.toString()));
        Path patches =
                root.resolve(
                        "profiles/"
                                + TestProfiles.declaring(root, PassionDisplayPowerGuard.NAME)
                                + "/patches");

        for (String helper : List.of("passion-api30-display-power", "passion-api30-nexus-power")) {
            javaSources(patches.resolve(helper + "/src"))
                    .forEach(file -> compile.add(file.toString()));
        }

        javaSources(root.resolve("src/prepare/test/fixtures/passion-power"))
                .forEach(file -> compile.add(file.toString()));
        compile.add(
                root.resolve("src/prepare/test/java/com/voyemu/display/PassionPowerHelperTest.java")
                        .toString());
        int compiled = run(work, "compile", compile);
        require(compiled == 0, Files.readString(work.resolve("compile.stderr")));
        int tested =
                run(
                        work,
                        "behavior",
                        List.of(
                                jdkTool(jdk, "java").toString(),
                                "-Xmx128m",
                                "-cp",
                                classes + java.io.File.pathSeparator + platform,
                                "com.voyemu.display.PassionPowerHelperTest"));
        Files.writeString(
                work.resolve("receipt.json"),
                Json.write(
                                Map.of(
                                        "compile_exit",
                                        compiled,
                                        "behavior_exit",
                                        tested,
                                        "guest_ART_verified",
                                        false,
                                        "guest_started",
                                        false))
                        + "\n");
        require(tested == 0, Files.readString(work.resolve("behavior.stderr")));
        System.out.print(Files.readString(work.resolve("behavior.stdout")));
    }

    private static int run(Path work, String name, List<String> command) throws Exception {
        return HostTestRunner.run(
                new ProcessBuilder(command)
                        .redirectOutput(work.resolve(name + ".stdout").toFile())
                        .redirectError(work.resolve(name + ".stderr").toFile()),
                30);
    }
}
