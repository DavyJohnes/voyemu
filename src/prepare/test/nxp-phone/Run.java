import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

import javax.tools.ToolProvider;

/** NXP Phone tests use the existing SDK and shared fixtures; no VM or downloaded dependencies. */
// Runs as a single-file source program, which has no package.
@SuppressWarnings("DefaultPackage")
class Run {
    static Path root, work, sdk;

    static void need(boolean value, String reason) {
        if (!value) {
            throw new AssertionError(reason);
        }
    }

    static List<Path> sources(Path folder) throws Exception {
        try (var paths = Files.walk(folder)) {
            return paths.filter(p -> p.toString().endsWith(".java")).sorted().toList();
        }
    }

    static void compile(Path output, List<Path> sources, String classpath) throws Exception {
        Files.createDirectories(output);
        var args =
                new ArrayList<String>(
                        List.of(
                                "--release",
                                "25",
                                "-encoding",
                                "UTF-8",
                                "-proc:none",
                                "-classpath",
                                classpath,
                                "-d",
                                output.toString()));
        sources.forEach(p -> args.add(p.toString()));
        need(
                ToolProvider.getSystemJavaCompiler()
                                .run(null, System.out, System.err, args.toArray(String[]::new))
                        == 0,
                "Compilation failed: " + output);
    }

    static void run(String cp, String type, String... values) throws Exception {
        var args =
                new ArrayList<String>(
                        List.of(
                                Path.of(
                                                System.getProperty("java.home"),
                                                "bin",
                                                System.getProperty("os.name").startsWith("Windows")
                                                        ? "java.exe"
                                                        : "java")
                                        .toString(),
                                "-Xmx512m",
                                "-cp",
                                cp,
                                type));

        args.addAll(List.of(values));
        need(new ProcessBuilder(args).inheritIO().start().waitFor() == 0, "Failed " + type);
    }

    static Path source(String relative) {
        return root.resolve(relative);
    }

    static void copyTree(Path original, Path destination) throws Exception {
        try (var paths = Files.walk(original)) {
            for (Path file : paths.toList()) {
                if (Files.isRegularFile(file)) {
                    Path target = destination.resolve(original.relativize(file));
                    Files.createDirectories(target.getParent());
                    Files.copy(file, target);
                }
            }
        }
    }

    static void prepareAdapterStubs(Path adapterStubs) throws Exception {
        copyTree(source("src/prepare/test/fixtures/phone-peer-process"), adapterStubs);
        Path log = adapterStubs.resolve("android/util/Log.java");
        String logSource = Files.readString(log);
        need(
                logSource.contains("if (!\"VoyemuPhonePeer\".equals(tag))"),
                "Shared Log fixture changed");
        Files.writeString(
                log,
                logSource.replace(
                        "if (!\"VoyemuPhonePeer\".equals(tag))",
                        "if (!\"VoyemuPhonePeer\".equals(tag) &&"
                                + " !\"VoyemuPhonePower\".equals(tag))"));

        // Run the production control provider and adapter together; only Android process/I/O
        // fixtures differ.
        for (String relative :
                List.of(
                        "android/content/ContentProvider.java",
                        "android/content/ContentValues.java",
                        "android/database/Cursor.java")) {
            Path target = adapterStubs.resolve(relative);
            Files.createDirectories(target.getParent());
            Files.copy(source("src/prepare/test/phone-provider/" + relative), target);
        }

        Path context = adapterStubs.resolve("android/content/Context.java");
        Files.writeString(
                context,
                Files.readString(context)
                        .replace(
                                "public class Context {",
                                "public class Context { public int checkCallingPermission(String"
                                        + " permission){return 0;}"));
        Path adapterBundle = adapterStubs.resolve("android/os/Bundle.java");
        Files.writeString(
                adapterBundle,
                Files.readString(adapterBundle)
                        .replace(
                                " public Object get(String key)",
                                " public long getLong(String key,long fallback){return"
                                    + " values.containsKey(key)?(Long)values.get(key):fallback;}"
                                    + " public void put(String key,Object"
                                    + " value){values.put(key,value);} public Object get(String"
                                    + " key)"));
        Path packageManager = adapterStubs.resolve("android/content/pm/PackageManager.java");
        Files.writeString(
                packageManager,
                Files.readString(packageManager)
                        .replace(
                                "public class PackageManager {",
                                "public class PackageManager { public static final int"
                                        + " PERMISSION_GRANTED=0;"));
    }

    static void runAdapterTests(String profile, Path component, Path provider, Path adapterStubs)
            throws Exception {
        var adapter = new ArrayList<Path>();

        for (String directory : List.of("android", "org", "com")) {
            adapter.addAll(sources(adapterStubs.resolve(directory)));
        }

        adapter.add(provider.resolve("src/com/voyemu/input/PhoneControlProvider.java"));
        adapter.add(
                source(
                        profile
                                + "patches/nxp-phone-provider/test/com/voyemu/input/NxpPhoneAtomicPowerTest.java"));
        // The OEM-shaped adapter base, device cache and contact beans of fixture/oem replace
        // their stub-cls declarations, which have signatures only.
        Path stubs = component.resolve("src/stub-cls/com/qinggan/bluetooth");
        Path oem = source("src/prepare/test/fixtures/phone-peer-process/fixture/oem");
        var doubled =
                List.of(
                        stubs.resolve("profile/BTAdapter.java"),
                        stubs.resolve("BTDeviceManager.java"),
                        stubs.resolve("bean/BTContactItem.java"),
                        stubs.resolve("bean/BTPhoneNumber.java"));

        for (Path file : sources(component.resolve("src"))) {
            if (!doubled.contains(file)) {
                adapter.add(file);
            }
        }

        adapter.add(oem.resolve("com/qinggan/bluetooth/profile/BTAdapter.java"));
        adapter.add(oem.resolve("com/qinggan/bluetooth/BTDeviceManager.java"));
        adapter.add(oem.resolve("com/qinggan/bluetooth/bean/BTContactItem.java"));
        adapter.add(oem.resolve("com/qinggan/bluetooth/bean/BTPhoneNumber.java"));
        adapter.addAll(sources(component.resolve("test")));
        adapter.add(source("src/prepare/java/com/voyemu/prepare/Json.java"));
        Path adapterClasses = work.resolve("adapter-classes");
        compile(adapterClasses, adapter, sdk.toString());
        String acp = adapterClasses + File.pathSeparator + sdk;

        for (String test : List.of("PeerPowerTest", "PeerDeliveryTest")) {
            run(acp, "com.qinggan.bluetooth.internal.ivt8." + test);
        }

        run(
                acp,
                "com.qinggan.bluetooth.internal.ivt8.AdapterPowerDeliveryTest",
                work.resolve("private-phone").toString());
        run(
                acp,
                "com.qinggan.bluetooth.internal.ivt8.AdapterAudioTest",
                work.resolve("private-audio").toString());
        run(
                acp,
                "com.voyemu.input.NxpPhoneAtomicPowerTest",
                work.resolve("private-atomic-power").toString());
    }

    static void runProviderTests(Path provider) throws Exception {
        Path providerStubs = work.resolve("provider-stubs");

        copyTree(source("src/prepare/test/phone-peer-provider"), providerStubs);
        Path bundle = providerStubs.resolve("android/os/Bundle.java");
        String bundleSource = Files.readString(bundle);
        need(bundleSource.contains("public void putInt"), "Shared Bundle fixture changed");
        Files.writeString(
                bundle,
                bundleSource.replace(
                        "public void putInt",
                        "public boolean getBoolean(String k){return (Boolean)get(k);}public void"
                                + " putInt"));
        var providers = new ArrayList<>(sources(providerStubs));
        providers.add(provider.resolve("src/com/voyemu/input/PhonePeerProvider.java"));

        for (Path test : sources(provider.resolve("test"))) {
            if (!test.getFileName().toString().equals("NxpPhoneAtomicPowerTest.java")) {
                providers.add(test);
            }
        }

        providers.add(source("src/prepare/test/java/com/voyemu/input/PhonePeerProviderTest.java"));
        Path providerClasses = work.resolve("provider-classes");
        compile(providerClasses, providers, "");
        run(providerClasses.toString(), "com.voyemu.input.NxpPhonePowerProviderTest");
        run(providerClasses.toString(), "com.voyemu.input.PhonePeerProviderTest");
    }

    static String helperClasspath() throws Exception {
        var jars = new ArrayList<Path>();

        try (var files = Files.list(source(".local/toolchains/java/libraries"))) {
            files.filter(p -> p.toString().endsWith(".jar")).sorted().forEach(jars::add);
        }

        for (String jar : List.of("d8", "apksigner")) {
            jars.add(source(".local/toolchains/java-sdk/build-tools/36.1.0/lib/" + jar + ".jar"));
        }

        String dependencies =
                String.join(File.pathSeparator, jars.stream().map(Path::toString).toList());
        var helper = new ArrayList<>(sources(source("src/prepare/java")));

        for (String test :
                List.of(
                        "NxpPhoneRecipeTest",
                        "NxpVoiceInitializationTest",
                        "NxpVoiceNativeTest",
                        "NxpVoiceInputsTest",
                        "NxpPhoneAbiTest",
                        "NxpSeatDependencyTest",
                        "NxpSystemPolicyDependencyTest",
                        "NxpSystemPolicyHardwareTest",
                        "NxpApkRuntimeCoverageTest",
                        "NxpPhoneComponentTest",
                        "NxpPhoneConnectAnrTest",
                        "PhoneManifestProvidersTest")) {
            helper.add(source("src/prepare/test/java/com/voyemu/prepare/" + test + ".java"));
        }

        Path helperClasses = work.resolve("helper-classes");
        compile(helperClasses, helper, dependencies);
        String hcp = helperClasses + File.pathSeparator + dependencies;

        return hcp;
    }

    static void runOemTests(String hcp) throws Exception {
        Path originals = source(".local/profiles/free-09-chinese-20221230-nxp-rc45/originals");

        if (!Files.isDirectory(originals)) {
            System.out.println("NXP OEM checks skipped, OEM inputs absent: " + originals);
        } else {
            run(
                    hcp,
                    "com.voyemu.prepare.NxpVoiceInitializationTest",
                    root.toString(),
                    work.resolve("voice-initialization").toString());
            run(
                    hcp,
                    "com.voyemu.prepare.NxpVoiceNativeTest",
                    root.toString(),
                    originals.toString());
            run(
                    hcp,
                    "com.voyemu.prepare.NxpVoiceInputsTest",
                    root.toString(),
                    originals.toString());
            run(hcp, "com.voyemu.prepare.NxpSeatDependencyTest", root.toString());
            run(hcp, "com.voyemu.prepare.NxpSystemPolicyDependencyTest", root.toString());
            run(
                    hcp,
                    "com.voyemu.prepare.NxpSystemPolicyHardwareTest",
                    root.toString(),
                    work.resolve("systempolicy-hardware").toString());
            run(hcp, "com.voyemu.prepare.NxpApkRuntimeCoverageTest", root.toString());
            run(
                    hcp,
                    "com.voyemu.prepare.NxpPhoneAbiTest",
                    root.toString(),
                    work.resolve("oem-abi").toString());
            run(
                    hcp,
                    "com.voyemu.prepare.PhoneManifestProvidersTest",
                    originals.resolve("bluetoothphone.apk").toString(),
                    work.resolve("AndroidManifest.xml").toString());
        }
    }

    public static void main(String[] args) throws Exception {
        need(args.length == 1, "Arguments: ROOT");
        root = Path.of(args[0]).toRealPath();
        sdk = source(".local/toolchains/java-sdk/platforms/android-34/android.jar");
        need(Files.isRegularFile(sdk), "Prepared SDK required");
        need(ToolProvider.getSystemJavaCompiler() != null, "Full JDK25 required");
        work = source(".local/build/jobs/nxp-phone-tests-" + UUID.randomUUID());
        Files.createDirectories(work);
        String profile = "profiles/free-09-chinese-20221230-nxp-rc45/";
        Path component = source(profile + "components/phone-bluetooth"),
                provider = source(profile + "patches/nxp-phone-provider");
        Path adapterStubs = work.resolve("adapter-stubs");
        prepareAdapterStubs(adapterStubs);
        runAdapterTests(profile, component, provider, adapterStubs);
        runProviderTests(provider);
        String hcp = helperClasspath();
        run(hcp, "com.voyemu.prepare.NxpPhoneRecipeTest", root.toString());
        run(hcp, "com.voyemu.prepare.NxpPhoneConnectAnrTest", root.toString());
        run(
                hcp,
                "com.voyemu.prepare.NxpPhoneComponentTest",
                root.toString(),
                work.resolve("ordinary-component").toString());
        runOemTests(hcp);
        System.out.println("PASS NXP Phone local contracts; evidence: " + work);
    }
}
