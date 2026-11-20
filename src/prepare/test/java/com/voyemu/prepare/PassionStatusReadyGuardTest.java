package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;
import java.util.zip.ZipFile;

public class PassionStatusReadyGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static int helperCalls(Method method) {
        int count = 0;

        for (var i : method.getImplementation().getInstructions()) {
            if (i instanceof ReferenceInstruction q
                    && q.getReference()
                            .toString()
                            .startsWith(PassionNavigationReadyGuard.HELPER + "->")) {
                count++;
            }
        }

        return count;
    }

    static void check(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    static void reject(Action a) throws Exception {
        try {
            a.run();
        } catch (Failure e) {
            return;
        }

        throw new AssertionError("accepted wrong contract");
    }

    static byte[] dex(Path apk) throws Exception {
        try (var z = new ZipFile(apk.toFile())) {
            return z.getInputStream(z.getEntry("classes3.dex")).readAllBytes();
        }
    }

    public static void main(String[] a) throws Exception {
        Path root = Path.of(a[0]), out = Path.of(a[1]);

        Files.createDirectories(out);
        Path original =
                root.resolve(
                        ".local/profiles/"
                                + TestProfiles.pinning(root, PassionStatusReadyGuard.APK)
                                + "/originals/LY_Launcher.apk");
        byte[] raw = dex(original);
        String rawBefore = NativeCore.hash(raw);
        byte[] pipeline = PassionSidebarGuard.apply(TestProfiles.SYNTHETIC, 30, original, raw);
        pipeline =
                PassionBluetoothVolumeGuard.apply(TestProfiles.SYNTHETIC, 30, original, pipeline);
        pipeline = PassionLauncherAudioGuard.apply(TestProfiles.SYNTHETIC, 30, original, pipeline);
        final byte[] prior =
                PassionNavigationReadyGuard.applyPrepared(
                        TestProfiles.SYNTHETIC, 30, original, pipeline);
        check(rawBefore.equals(NativeCore.hash(raw)));
        byte[] changed =
                PassionStatusReadyGuard.applyPrepared(TestProfiles.SYNTHETIC, 30, original, prior);
        var before = PassionServiceForegroundGuard.classes(prior);
        var after = PassionServiceForegroundGuard.classes(changed);
        int negative = 0, sites = 0;

        for (var target : PassionStatusReadyGuard.TARGETS) {
            Method old = PassionStatusReadyGuard.find(before, target),
                    m = PassionStatusReadyGuard.find(after, target);
            PassionStatusReadyGuard.validateMethod(
                    PassionStatusReadyGuard.rewrite(m, old, target, true), target);
            int oldUnits = 0, newUnits = 0;

            for (var i : old.getImplementation().getInstructions()) {
                oldUnits += i.getCodeUnits();
            }

            for (var i : m.getImplementation().getInstructions()) {
                newUnits += i.getCodeUnits();
            }

            check(
                    oldUnits == newUnits
                            && old.getImplementation().getRegisterCount()
                                    == m.getImplementation().getRegisterCount());

            for (var site : target.sites()) {
                sites++;
                var body = new MutableMethodImplementation(m.getImplementation());
                int at = PassionServiceForegroundGuard.index(body, site.invoke());
                var call = body.getInstructions().get(at);
                check(
                        call.getOpcode() == Opcode.INVOKE_STATIC
                                && ((FiveRegisterInstruction) call).getRegisterC()
                                        == site.receiver());

                if (site.second() >= 0) {
                    check(((FiveRegisterInstruction) call).getRegisterD() == site.second());
                }

                body.replaceInstruction(
                        at,
                        new BuilderInstruction35c(
                                Opcode.INVOKE_STATIC,
                                site.second() < 0 ? 1 : 2,
                                (site.receiver() + 1) % target.registers(),
                                site.second() < 0 ? 0 : site.second(),
                                0,
                                0,
                                0,
                                ((ReferenceInstruction) call).getReference()));
                var bad = PassionServiceForegroundGuard.copy(m, body);
                reject(() -> PassionStatusReadyGuard.validateAfter(old, bad, target));
                negative++;
            }
        }

        reject(() ->
                PassionStatusReadyGuard.applyPrepared(TestProfiles.SYNTHETIC, 29, original, prior));
        negative++;
        reject(() ->
                PassionStatusReadyGuard.applyPrepared(
                        TestProfiles.SYNTHETIC,
                        30,
                        root.resolve(
                                ".local/profiles/"
                                        + TestProfiles.pinning(root, PassionStatusReadyGuard.APK)
                                        + "/originals/LY_AVM.apk"),
                        prior));
        negative++;

        // The existing three Nav helper calls stay exactly unchanged outside these five regions.
        for (var nav : PassionNavigationReadyGuard.TARGETS) {
            check(
                    helperCalls(PassionNavigationReadyGuard.find(before, nav)) == 1
                            && helperCalls(PassionNavigationReadyGuard.find(after, nav)) == 1);
        }

        PassionStatusReadyGuard.validateHelperSource(
                root.resolve(
                        "profiles/"
                                + TestProfiles.pinning(root, PassionStatusReadyGuard.APK)
                                + "/patches/passion-api30-status-ready/src"));
        Files.write(out.resolve("status.dex"), changed);
        Files.writeString(
                out.resolve("receipt.json"),
                Json.write(
                                Map.of(
                                        "sites",
                                        sites,
                                        "methods",
                                        4,
                                        "negative_checks",
                                        negative,
                                        "all_class_inverse",
                                        true,
                                        "RAW_pass0_readonly",
                                        true,
                                        "strict_actual_predecessor",
                                        true,
                                        "existing_Nav_calls_preserved",
                                        true,
                                        "output_sha256",
                                        NativeCore.hash(changed),
                                        "ART_verified",
                                        false,
                                        "runtime_verified",
                                        false))
                        + "\n");
        System.out.println(
                "PASS Status5sites/"
                        + negative
                        + " negatives/all-class inverse/rawpass0/actualpredecessor; ART/runtime"
                        + " pending");
    }
}
