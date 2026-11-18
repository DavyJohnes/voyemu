package com.voyemu.prepare;

import static com.voyemu.prepare.HostTestRunner.javaSources;
import static com.voyemu.prepare.HostTestRunner.jdkTool;
import static com.voyemu.prepare.HostTestRunner.pathOptions;
import static com.voyemu.prepare.HostTestRunner.require;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

/** Compile and check the pinned Passion MCU property guard and the Android helper behavior. */
public final class PassionMcuPropertyHostTest {
    public static void main(String[] args) throws Exception {
        if (args.length == 1 && List.of("-h", "--help").contains(args[0])) {
            System.out.println(
                    "PassionMcuPropertyHostTest --root ROOT --work WORK --toolchain TOOLCHAIN"
                            + " --platform PLATFORM --original APK");

            return;
        }

        var paths =
                pathOptions(args, "--root", "--work", "--toolchain", "--platform", "--original");
        Path root = paths.get("--root").toRealPath();
        Path work = paths.get("--work");
        Path toolchain = paths.get("--toolchain").toRealPath();
        Path platform = paths.get("--platform").toRealPath();
        Path original = paths.get("--original").toRealPath();
        Files.createDirectories(work.getParent());
        Files.createDirectory(work);

        String classpath =
                toolchain.resolve("voyemu-java-helper.jar")
                        + File.pathSeparator
                        + toolchain.resolve("lib")
                        + File.separator
                        + "*";
        Path source =
                root.resolve(
                        "profiles/passion-11-russian-20260403-2.1.4.80/patches/passion-api30-mcu-property/src");
        Path prepare = root.resolve("src/prepare/java/com/voyemu/prepare");
        Path tests = root.resolve("src/prepare/test/java/com/voyemu");
        Path guard = work.resolve("guard");
        Path behavior = work.resolve("behavior");

        run(
                root,
                List.of(
                        jdkTool("javac").toString(),
                        "-J-Xmx128m",
                        "-cp",
                        classpath,
                        "-d",
                        guard.toString(),
                        prepare.resolve("PassionMcuPropertyGuard.java").toString(),
                        prepare.resolve("PassionMcuManifest.java").toString(),
                        prepare.resolve("PassionMcuHelperContract.java").toString(),
                        prepare.resolve("PreparationMain.java").toString(),
                        tests.resolve("prepare/PassionMcuPropertyGuardTest.java").toString()));
        run(
                root,
                List.of(
                        jdkTool("java").toString(),
                        "-Xmx128m",
                        "-ea",
                        "-cp",
                        guard + File.pathSeparator + classpath,
                        "com.voyemu.prepare.PassionMcuPropertyGuardTest",
                        original.toString(),
                        work.resolve("proof").toString(),
                        source.toString(),
                        platform.toString()));

        var compile =
                new ArrayList<>(
                        List.of(
                                jdkTool("javac").toString(),
                                "-J-Xmx128m",
                                "-d",
                                behavior.toString()));
        javaSources(root.resolve("src/prepare/test/fixtures/passion-mcu-property"))
                .forEach(file -> compile.add(file.toString()));
        compile.add(source.resolve("com/voyemu/vehicle/PassionMcuProperty.java").toString());
        compile.add(source.resolve("com/voyemu/vehicle/PassionMcuProtocol.java").toString());
        javaSources(tests.resolve("vehicle"), "PassionMcu*Test.java")
                .forEach(file -> compile.add(file.toString()));
        run(root, compile);

        for (String test : List.of("PassionMcuPropertyTest", "PassionMcuProtocolTest")) {
            run(
                    root,
                    List.of(
                            jdkTool("java").toString(),
                            "-Xmx64m",
                            "-ea",
                            "-cp",
                            behavior.toString(),
                            "com.voyemu.vehicle." + test));
        }
    }

    private static void run(Path root, List<String> command) throws Exception {
        int status =
                HostTestRunner.run(
                        new ProcessBuilder(command).directory(root.toFile()).inheritIO(), 60);

        require(status == 0, "Exit " + status + ": " + command);
    }
}
