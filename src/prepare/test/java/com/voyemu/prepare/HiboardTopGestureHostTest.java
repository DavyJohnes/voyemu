package com.voyemu.prepare;

import static com.voyemu.prepare.HostTestRunner.javaSources;
import static com.voyemu.prepare.HostTestRunner.jdkTool;
import static com.voyemu.prepare.HostTestRunner.pathOptions;
import static com.voyemu.prepare.HostTestRunner.require;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** Host regression for the Free A11 7.3.1 OEM top-gesture side-effect adapter. */
public final class HiboardTopGestureHostTest {
    private static final String TEST_PACKAGE = "com.android.server.wm.";
    private static final String GESTURE_TEST = "HiboardTopGestureTest";
    private static final String OCCLUSION_TEST = "HiboardTopGestureOcclusionTest";

    public static void main(String[] args) throws Exception {
        if (args.length == 1 && List.of("-h", "--help").contains(args[0])) {
            System.out.println("HiboardTopGestureHostTest --root ROOT --work WORK --jdk JDK");

            return;
        }

        var paths = pathOptions(args, "--root", "--work", "--jdk");
        Path root = paths.get("--root").toRealPath();
        Path work = paths.get("--work");
        Path jdk = paths.get("--jdk");
        Files.createDirectories(work.getParent());
        Files.createDirectory(work);
        Path classes = work.resolve("classes");

        Path source =
                root.resolve(
                        "profiles/free-11-chinese-20260731-7.3.1/patches"
                            + "/api30-hiboard-top-gesture/src/com/android/server/wm/HiboardTopGesture.java");
        Path fixtures = root.resolve("src/prepare/test/fixtures/hiboard-top-gesture");
        Path tests = root.resolve("src/prepare/test/java/com/android/server/wm");
        var compile =
                new ArrayList<>(
                        List.of(
                                jdkTool(jdk, "javac").toString(),
                                "--release",
                                "8",
                                "-d",
                                classes.toString(),
                                source.toString()));
        javaSources(fixtures).forEach(file -> compile.add(file.toString()));
        javaSources(tests, "HiboardTopGesture*Test.java")
                .forEach(file -> compile.add(file.toString()));

        var checks = new ArrayList<Map<String, Object>>();
        Result result = run(work, checks, "compile", compile);
        require(result.status() == 0, result.stderr());
        result =
                run(
                        work,
                        checks,
                        "original-red",
                        List.of(
                                jdkTool(jdk, "java").toString(),
                                "-Xmx128m",
                                "-cp",
                                classes.toString(),
                                TEST_PACKAGE + GESTURE_TEST,
                                "--original"));
        require(
                result.status() != 0 && result.stderr().contains("RED original stock side effect"),
                "Expected the original stock side effect to fail: " + result);

        for (String test : List.of(GESTURE_TEST, OCCLUSION_TEST)) {
            result =
                    run(
                            work,
                            checks,
                            test,
                            List.of(
                                    jdkTool(jdk, "java").toString(),
                                    "-Xmx128m",
                                    "-cp",
                                    classes.toString(),
                                    TEST_PACKAGE + test));
            require(result.status() == 0, result.stderr());
        }

        var receipt = new LinkedHashMap<String, Object>();
        receipt.put("checks", checks);
        receipt.put("guest_ART_verified", false);
        receipt.put("guest_started", false);
        Files.writeString(work.resolve("receipt.json"), Json.write(receipt) + "\n");
        System.out.println(
                "PASS helper RED/GREEN, 30 boundary cases, 9 z-order/transparent/modal cases; guest"
                        + " ART pending");
    }

    private static Result run(
            Path work, List<Map<String, Object>> checks, String name, List<String> command)
            throws Exception {
        Path stdout = work.resolve(name + ".stdout");
        Path stderr = work.resolve(name + ".stderr");
        int status =
                HostTestRunner.run(
                        new ProcessBuilder(command)
                                .redirectOutput(stdout.toFile())
                                .redirectError(stderr.toFile()),
                        30);
        var check = new LinkedHashMap<String, Object>();

        check.put("name", name);
        check.put("exit", status);
        checks.add(check);

        return new Result(status, Files.readString(stderr));
    }

    private record Result(int status, String stderr) {}
}
