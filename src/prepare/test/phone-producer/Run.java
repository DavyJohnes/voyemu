import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeMap;
import java.util.UUID;

import javax.tools.ToolProvider;

/** Normal pinned helper dependencies; optional overlay keeps root review runs isolated. */
// Runs as a single-file source program, which has no package.
@SuppressWarnings("DefaultPackage")
class Run {
    static void need(boolean value, String reason) {
        if (!value) {
            throw new AssertionError(reason);
        }
    }

    static List<Path> javaFiles(Path folder) throws Exception {
        try (var walk = Files.walk(folder)) {
            return walk.filter(p -> p.toString().endsWith(".java")).sorted().toList();
        }
    }

    static void run(Path java, String cp, String type, List<String> args) throws Exception {
        var command = new ArrayList<String>(List.of(java.toString(), "-Xmx512m", "-cp", cp, type));

        command.addAll(args);
        need(new ProcessBuilder(command).inheritIO().start().waitFor() == 0, "Failed " + type);
    }

    public static void main(String[] args) throws Exception {
        need(
                args.length == 2 || args.length == 3,
                "Arguments: ROOT exact-Phone-APK [isolated-source-overlay]");
        Path root = Path.of(args[0]).toRealPath(),
                apk = Path.of(args[1]).toRealPath(),
                overlay = args.length == 3 ? Path.of(args[2]).toRealPath() : root;
        String id = "free-11-chinese-20260731-7.3.1";
        Path profile = overlay.resolve("profiles/" + id);
        Path platform = root.resolve(".local/toolchains/java-sdk/platforms/android-34/android.jar");
        need(Files.isRegularFile(platform), "Normal prepared SDK34 platform required");
        Path work = root.resolve(".local/build/jobs/phone-producer-tests-" + UUID.randomUUID());
        Files.createDirectories(work);
        Files.copy(
                profile.resolve("patches/api30-phone-provider.json"),
                work.resolve("api30-phone-provider.json"));
        var deps = new ArrayList<Path>();

        try (var walk = Files.list(root.resolve(".local/toolchains/java/libraries"))) {
            walk.filter(p -> p.toString().endsWith(".jar")).sorted().forEach(deps::add);
        }

        for (String n : List.of("d8", "apksigner")) {
            Path jar =
                    root.resolve(".local/toolchains/java-sdk/build-tools/36.1.0/lib/" + n + ".jar");
            need(Files.isRegularFile(jar), "Normal prepared " + n + " dependency required");
            deps.add(jar);
        }

        String depCp = String.join(File.pathSeparator, deps.stream().map(Path::toString).toList());
        var sources = new TreeMap<String, Path>();

        for (Path p : javaFiles(root.resolve("src/prepare/java"))) {
            sources.put(root.relativize(p).toString(), p);
        }

        if (!overlay.equals(root)) {
            for (Path p : javaFiles(overlay.resolve("src/prepare/java"))) {
                sources.put(overlay.relativize(p).toString(), p);
            }
        }

        for (String n :
                List.of(
                        "PhoneManifestPreserveTest",
                        "PhoneProviderTest",
                        "PhonePackageTest",
                        "PhonePeerTest",
                        "PhoneDisconnectGuardTest")) {
            sources.put(
                    n, overlay.resolve("src/prepare/test/java/com/voyemu/prepare/" + n + ".java"));
        }

        sources.put(
                "originalManifestTest",
                root.resolve(
                        "src/prepare/test/java/com/voyemu/prepare/PhoneManifestProvidersTest.java"));
        Path classes = work.resolve("host-classes");
        Files.createDirectories(classes);
        var compile =
                new ArrayList<String>(
                        List.of(
                                "--release",
                                "25",
                                "-encoding",
                                "UTF-8",
                                "-proc:none",
                                "-classpath",
                                depCp,
                                "-d",
                                classes.toString()));
        sources.values().forEach(p -> compile.add(p.toString()));
        need(ToolProvider.getSystemJavaCompiler() != null, "Full JDK25 required");
        need(
                ToolProvider.getSystemJavaCompiler()
                                .run(null, System.out, System.err, compile.toArray(String[]::new))
                        == 0,
                "Normal helper compilation failed");
        String cp = classes + File.pathSeparator + depCp;
        Path java =
                Path.of(
                        System.getProperty("java.home"),
                        "bin",
                        System.getProperty("os.name").startsWith("Windows") ? "java.exe" : "java");
        run(java, cp, "com.voyemu.prepare.PhoneDisconnectGuardTest", List.of(apk.toString()));
        run(
                java,
                cp,
                "com.voyemu.prepare.PhoneProviderTest",
                List.of(root.toString(), work.toString(), platform.toString(), apk.toString()));
        run(
                java,
                cp,
                "com.voyemu.prepare.PhoneManifestPreserveTest",
                List.of(
                        apk.toString(),
                        root.resolve(
                                        ".local/profiles/"
                                                + id
                                                + "/originals/theme/defaulttheme/simple/BluetoothPhone-release-signed.apk")
                                .toString()));
        run(
                java,
                cp,
                "com.voyemu.prepare.PhonePackageTest",
                List.of(root.toString(), work.toString(), apk.toString(), profile.toString()));
        run(
                java,
                cp,
                "com.voyemu.prepare.PhonePeerTest",
                List.of(root.toString(), work.toString(), apk.toString(), profile.toString()));
        System.out.println("PASS ordinary API30 phone graph; retained evidence: " + work);
    }
}
