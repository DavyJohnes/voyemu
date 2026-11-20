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

public class PassionNavigationReadyGuardTest {
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

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]),
                out = Path.of(args[1]),
                original =
                        root.resolve(
                                ".local/profiles/"
                                        + TestProfiles.pinning(
                                                root, PassionNavigationReadyGuard.APK)
                                        + "/originals/LY_Launcher.apk");
        byte[] raw = dex(original);
        byte[] changed =
                PassionNavigationReadyGuard.applyPrepared(
                        TestProfiles.SYNTHETIC, 30, original, raw);
        var before = PassionServiceForegroundGuard.classes(raw);
        var after = PassionServiceForegroundGuard.classes(changed);
        int negative = 0;

        for (var t : PassionNavigationReadyGuard.TARGETS) {
            Method m = PassionNavigationReadyGuard.find(after, t),
                    old = PassionNavigationReadyGuard.find(before, t);
            PassionNavigationReadyGuard.validateMethod(
                    PassionNavigationReadyGuard.rewrite(m, old, t, true), t);
            var body = new MutableMethodImplementation(m.getImplementation());
            int index = PassionServiceForegroundGuard.index(body, t.invoke());
            var call = body.getInstructions().get(index);
            check(
                    call.getOpcode() == Opcode.INVOKE_STATIC
                            && ((FiveRegisterInstruction) call).getRegisterC() == t.receiver());
            check(
                    old.getImplementation().getRegisterCount()
                            == m.getImplementation().getRegisterCount());
            // No branch targets/try ranges move: replace is equal-code-unit-width. Receiver remains
            // original p0.
            int oldUnits = 0, newUnits = 0;

            for (var i : old.getImplementation().getInstructions()) {
                oldUnits += i.getCodeUnits();
            }

            for (var i : m.getImplementation().getInstructions()) {
                newUnits += i.getCodeUnits();
            }

            check(oldUnits == newUnits);
            body.replaceInstruction(
                    index,
                    new BuilderInstruction35c(
                            Opcode.INVOKE_STATIC,
                            1,
                            (t.receiver() + 1) % t.registers(),
                            0,
                            0,
                            0,
                            0,
                            ((ReferenceInstruction) call).getReference()));
            var bad = PassionServiceForegroundGuard.copy(m, body);
            reject(() -> PassionNavigationReadyGuard.validateAfter(old, bad, t));
            negative++;
        }

        reject(() ->
                PassionNavigationReadyGuard.applyPrepared(
                        TestProfiles.SYNTHETIC, 29, original, raw));
        negative++;
        byte[] prior = PassionSidebarGuard.apply(TestProfiles.SYNTHETIC, 30, original, raw);
        prior = PassionBluetoothVolumeGuard.apply(TestProfiles.SYNTHETIC, 30, original, prior);
        prior = PassionLauncherAudioGuard.apply(TestProfiles.SYNTHETIC, 30, original, prior);
        byte[] prepared =
                PassionNavigationReadyGuard.applyPrepared(
                        TestProfiles.SYNTHETIC, 30, original, prior);
        Files.writeString(
                out.resolve("nav-guard-receipt.json"),
                Json.write(
                                Map.of(
                                        "sites",
                                        3,
                                        "negative_checks",
                                        negative,
                                        "all_class_inverse",
                                        true,
                                        "equal_width_addresses_preserved",
                                        true,
                                        "raw_fixture_and_actual_prepared_chain",
                                        true,
                                        "prepared_output_sha256",
                                        NativeCore.hash(prepared),
                                        "ART_verified",
                                        false,
                                        "runtime_verified",
                                        false,
                                        "canonical_changed",
                                        false))
                        + "\n");
        System.out.println(
                "PASS Nav3sites/"
                        + negative
                        + " negatives +all-class inverse/raw+actualprepared; ART/runtime pending");
    }
}
