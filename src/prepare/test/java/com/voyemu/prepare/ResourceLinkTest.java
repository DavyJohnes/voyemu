package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.List;

/** Actual pinned aapt2 regression; no assertion that merely mirrors command text. */
public final class ResourceLinkTest {
    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            throw new IllegalArgumentException("PROJECT_ROOT AAPT2 ANDROID_JAR");
        }

        Path project = Path.of(args[0]).toRealPath(),
                aapt2 = Path.of(args[1]).toRealPath(),
                platform = Path.of(args[2]).toRealPath();

        if (!Files.isDirectory(project.resolve("src/prepare"))) {
            throw new AssertionError("Voyemu project root required");
        }

        Path parent = project.resolve(".local/build/tmp");
        Storage.directory(parent);
        Path root = Files.createTempDirectory(parent, "voyemu-resource-link-");

        try {
            Path source = root.resolve("source"), work = root.resolve("work");
            Storage.directory(source.resolve("res/values"));
            Storage.directory(work);
            Path manifest = source.resolve("AndroidManifest.xml"),
                    values = source.resolve("res/values/dimens.xml");
            Files.writeString(
                    manifest,
                    "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\""
                            + " package=\"own.test.overlay\"><uses-sdk android:minSdkVersion=\"28\""
                            + " android:targetSdkVersion=\"28\"/><overlay"
                            + " android:targetPackage=\"android\" android:isStatic=\"false\""
                            + " android:priority=\"999\"/><application"
                            + " android:hasCode=\"false\"/></manifest>\n");
            Files.writeString(
                    values,
                    "<resources><dimen name=\"voyemu_test_height\">0dp</dimen></resources>\n");
            String manifestPin = FilesSupport.hash(manifest), valuesPin = FilesSupport.hash(values);
            Path apk = Artifacts.linkComponentResources(aapt2, platform, source, work, 28, 28);
            var entries = NativeCore.entries(apk);

            if (!entries.containsKey("resources.arsc")
                    || !entries.containsKey("AndroidManifest.xml")) {
                throw new AssertionError("resource APK missing compiled table/manifest");
            }

            var dumped =
                    Processes.run(
                            aapt2,
                            List.of("dump", "resources", apk.toString()),
                            work,
                            Duration.ofSeconds(30),
                            false);

            if (!dumped.stdout().contains("voyemu_test_height")) {
                throw new AssertionError(
                        "new dimension missing from actual compiled resource table");
            }

            var old =
                    Processes.run(
                            aapt2,
                            List.of(
                                    "link",
                                    "-o",
                                    work.resolve("old-overlay.apk").toString(),
                                    "--manifest",
                                    manifest.toString(),
                                    "-I",
                                    platform.toString(),
                                    "-R",
                                    work.resolve("compiled.zip").toString()),
                            work,
                            Duration.ofSeconds(30),
                            true);

            if (old.exitCode() == 0
                    || !old.stderr().contains("does not override an existing resource")) {
                throw new AssertionError("old erroneous -R path not reproduced");
            }

            if (!FilesSupport.hash(manifest).equals(manifestPin)
                    || !FilesSupport.hash(values).equals(valuesPin)) {
                throw new AssertionError("link changed source resources");
            }

            System.out.println(
                    "actual aapt2 new-resource table links; previous overlay failure reproduced");
        } finally {
            FilesSupport.removeOwnedTree(root);
        }
    }
}
