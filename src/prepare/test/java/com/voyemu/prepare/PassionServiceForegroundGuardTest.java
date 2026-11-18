package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction21c;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;
import org.jf.dexlib2.immutable.reference.ImmutableStringReference;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

public final class PassionServiceForegroundGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static void check(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    static void reject(Action a) throws Exception {
        try {
            a.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("contract unexpectedly accepted");
    }

    static byte[] dex(Path p, String entry) throws Exception {
        try (var z = new ZipFile(p.toFile())) {
            return z.getInputStream(z.getEntry(entry)).readAllBytes();
        }
    }

    static Method method(byte[] d, PassionServiceForegroundGuard.Target t) {
        for (var c : PassionServiceForegroundGuard.classes(d).values()) {
            for (var m : c.getMethods()) {
                if (PassionServiceForegroundGuard.target(m, List.of(t)) != null) {
                    return m;
                }
            }
        }

        throw new AssertionError();
    }

    public static void main(String[] a) throws Exception {
        Path root = Path.of(a[0]), stage = Path.of(a[1]);

        Files.createDirectory(stage);
        int negatives = 0, sites = 0;

        for (String name : List.of("LY_Video", "LY_Camera")) {
            Path original =
                    root.resolve(
                            ".local/profiles/"
                                    + PassionServiceForegroundGuard.PROFILE
                                    + "/originals/"
                                    + name
                                    + ".apk");
            byte[] before = dex(original, "classes2.dex");
            String pin = FilesSupport.hash(original);
            var targets = PassionServiceForegroundGuard.targets(pin);

            for (var t : targets) {
                var m = method(before, t);
                PassionServiceForegroundGuard.validateOriginal(m, t);
                reject(() -> PassionServiceForegroundGuard.validateAfter(m, t));
                negatives++;
            }

            byte[] after =
                    PassionServiceForegroundGuard.apply(
                            PassionServiceForegroundGuard.PROFILE, 30, original, before);

            for (var t : targets) {
                var m = method(after, t);
                PassionServiceForegroundGuard.validateAfter(m, t);
                sites++;
                negatives += rejectSiteMutations(m, t);
            }

            reject(() ->
                    PassionServiceForegroundGuard.apply(
                            "free-11-chinese-20260731-7.3.1", 30, original, before));
            negatives++;
            reject(() ->
                    PassionServiceForegroundGuard.apply(
                            PassionServiceForegroundGuard.PROFILE, 28, original, before));
            negatives++;
            reject(() ->
                    PassionServiceForegroundGuard.apply(
                            PassionServiceForegroundGuard.PROFILE, 30, original, after));
            negatives++;
            Path out = stage.resolve(name + "-classes2.dex");
            Files.write(out, after);
            var child =
                    new ProcessBuilder(
                                    root.resolve(
                                                    ".local/toolchains/native-sdk/build-tools/36.1.0/dexdump")
                                            .toString(),
                                    "-c",
                                    out.toString())
                            .redirectErrorStream(true)
                            .redirectOutput(stage.resolve(name + "-checksum.log").toFile())
                            .start();
            check(child.waitFor() == 0);
        }

        Path launcher =
                root.resolve(
                        ".local/profiles/"
                                + PassionServiceForegroundGuard.PROFILE
                                + "/originals/LY_Launcher.apk");
        byte[] launcherDex = dex(launcher, "classes3.dex");
        reject(() ->
                PassionServiceForegroundGuard.apply(
                        PassionServiceForegroundGuard.PROFILE, 30, launcher, launcherDex));
        negatives++;
        int launcherMethods = launcherForegroundMethods(launcherDex);
        check(launcherMethods == 1 && sites == 3);
        Files.writeString(
                stage.resolve("receipt.json"),
                Json.write(
                                Map.of(
                                        "service_sites",
                                        sites,
                                        "negative_checks",
                                        negatives,
                                        "exact_original_APK_DEX_method_guards",
                                        true,
                                        "semantic_inverse_all_classes",
                                        true,
                                        "no_inserted_register_writes",
                                        true,
                                        "media_playback_unchanged",
                                        true,
                                        "Launcher_foreground_absent_and_not_modified",
                                        true,
                                        "checksum_verified",
                                        true,
                                        "runtime_quiet_verified",
                                        false,
                                        "heard_boot_owner_attributed",
                                        false))
                        + "\n");
        System.out.println(
                "three exact service-status channels silent; "
                        + negatives
                        + " negatives; inverse+checksum PASS; runtime pending");
    }

    static int rejectSiteMutations(Method m, PassionServiceForegroundGuard.Target t)
            throws Exception {
        int negatives = 0;
        var body = new MutableMethodImplementation(m.getImplementation());

        body.replaceInstruction(
                PassionServiceForegroundGuard.index(body, t.idOffset()),
                new BuilderInstruction21c(
                        Opcode.CONST_STRING,
                        t.idRegister(),
                        new ImmutableStringReference(t.oldId())));
        reject(() ->
                PassionServiceForegroundGuard.validateAfter(
                        PassionServiceForegroundGuard.copy(m, body), t));
        negatives++;
        var nonnull = new MutableMethodImplementation(m.getImplementation());
        nonnull.replaceInstruction(
                PassionServiceForegroundGuard.index(nonnull, t.publication()),
                new BuilderInstruction35c(
                        Opcode.INVOKE_VIRTUAL,
                        3,
                        t.channel(),
                        t.zero(),
                        t.idRegister(),
                        0,
                        0,
                        new ImmutableMethodReference(
                                "Landroid/app/NotificationChannel;",
                                "setSound",
                                List.of("Landroid/net/Uri;", "Landroid/media/AudioAttributes;"),
                                "V")));
        reject(() ->
                PassionServiceForegroundGuard.validateAfter(
                        PassionServiceForegroundGuard.copy(m, nonnull), t));
        negatives++;
        var missing = new MutableMethodImplementation(m.getImplementation());
        missing.removeInstruction(PassionServiceForegroundGuard.index(missing, t.publication()));
        reject(() ->
                PassionServiceForegroundGuard.validateAfter(
                        PassionServiceForegroundGuard.copy(m, missing), t));
        negatives++;

        return negatives;
    }

    static int launcherForegroundMethods(byte[] launcherDex) {
        int launcherMethods = 0;

        for (var c : PassionServiceForegroundGuard.classes(launcherDex).values()) {
            if (c.getType().equals("Lcom/voyah/os/carsystemui/SystemuiService;")) {
                for (var m : c.getMethods()) {
                    if (m.getName().equals("onCreate")) {
                        launcherMethods++;

                        for (var i : m.getImplementation().getInstructions()) {
                            if (i instanceof ReferenceInstruction r) {
                                check(
                                        !r.getReference()
                                                        .toString()
                                                        .contains("NotificationChannel;")
                                                && !r.getReference()
                                                        .toString()
                                                        .contains("startForeground"));
                            }
                        }
                    }
                }
            }
        }

        return launcherMethods;
    }
}
