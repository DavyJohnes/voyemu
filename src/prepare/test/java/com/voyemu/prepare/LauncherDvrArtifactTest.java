package com.voyemu.prepare;

import com.android.apksig.ApkVerifier;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public final class LauncherDvrArtifactTest {
    static final String PROFILE = "free-11-chinese-20260731-7.3.1";

    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] a) throws Exception {
        Path root = Path.of(a[0]).toRealPath(),
                request = Path.of(a[1]).toRealPath(),
                work = Path.of(a[2]).toAbsolutePath();
        var selected = Json.obj(Json.object(Json.parse(Files.readString(request))), "tools");
        var sign = Json.obj(selected, "signing");
        var platform = Json.obj(selected, "platform");
        var registry = new ProfileRegistry(root, PROFILE);
        var profile = registry.get(PROFILE);
        var steps =
                List.of(
                        "framework-libraries.json",
                        "api30-launcher-power.json",
                        "api30-aqy-provider.json",
                        "api30-launcher-canbus.json",
                        LauncherDvrGuard.NAME + ".json");

        check(
                Json.obj(Json.obj(profile.data(), "patches"), "Launcher")
                        .get("steps")
                        .equals(steps),
                "exact Launcher recipe chain");
        var storage =
                new Storage(
                        root.resolve(".local"),
                        work.resolve("config"),
                        work.resolve("cache"),
                        work.resolve("state"),
                        work.resolve("run"),
                        work.resolve("logs"));
        var tools =
                new Toolchains(
                        storage,
                        Json.object(
                                Json.parse(
                                        Files.readString(root.resolve("src/app/toolchain.json")))),
                        Path.of(Json.str(selected, "sdk")),
                        Path.of(Json.str(sign, "key")),
                        Path.of(Json.str(sign, "certificate")),
                        Path.of(Json.str(platform, "file")));
        var log = new ArrayList<String>();
        var artifacts =
                new Artifacts(
                        new Artifacts.PatchedProfile(
                                profile, ProfileRegistry.patchMetadata(profile)),
                        storage,
                        tools,
                        root.resolve("profiles/" + PROFILE),
                        work,
                        p -> {
                            throw new AssertionError("unexpected Android read " + p);
                        },
                        log::add);
        Path apk = artifacts.resolve("patched:Launcher");
        var expected = new ArrayList<String>();

        for (int pass = 0; pass < 2; pass++) {
            for (var step : steps) {
                expected.add("Applying patch Launcher: " + step);
            }
        }

        check(log.equals(expected), "two pass recipe plan/apply");
        var entries = NativeCore.entries(apk);
        var classes = LauncherDvrGuard.classes(entries.get("classes2.dex"));

        for (String name : LauncherDvrGuard.Pinned.METHODS) {
            long calls = 0;

            for (var i :
                    LauncherDvrGuard.Pinned.method(classes, name)
                            .getImplementation()
                            .getInstructions()) {
                if (i instanceof org.jf.dexlib2.iface.instruction.ReferenceInstruction r
                        && r.getReference()
                                .toString()
                                .equals(LauncherDvrGuard.HELPER + "->select(I)I")) {
                    calls++;
                }
            }

            check(calls == 1, "DVR selector missing");
        }

        boolean helper = false;

        for (var e : entries.entrySet()) {
            if (e.getKey().matches("classes[0-9]+\\.dex")) {
                var dex = LauncherDvrGuard.classes(e.getValue());

                if (dex.containsKey(LauncherDvrGuard.HELPER)) {
                    helper = true;
                }
            }
        }

        check(helper, "compiled helper missing");
        var verified =
                new ApkVerifier.Builder(apk.toFile())
                        .setMinCheckedPlatformVersion(30)
                        .build()
                        .verify();
        check(
                verified.isVerified()
                        && verified.getSignerCertificates().size() == 1
                        && NativeCore.hash(verified.getSignerCertificates().get(0).getEncoded())
                                .equals(
                                        "c8a2e9bccf597c2fb6dc66bee293fc13f2fc47ec77bc6b2b0d52c11f51192ab8"),
                "platform signature");
        System.out.println(
                "PASS actual Launcher five-step recipe plan/apply, helper dex and platform"
                        + " signing");
    }
}
