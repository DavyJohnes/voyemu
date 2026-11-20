package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction35c;
import org.jf.dexlib2.iface.instruction.FiveRegisterInstruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

public final class PassionBluetoothVolumeGuardTest {
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

        throw new AssertionError("Unexpected acceptance");
    }

    static byte[] dex(Path p, String name) throws Exception {
        try (var z = new ZipFile(p.toFile())) {
            return z.getInputStream(z.getEntry(name)).readAllBytes();
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), stage = Path.of(args[1]);

        Files.createDirectory(stage);
        int negatives = 0, methods = 0, calls = 0;

        for (String role : List.of("LY_Launcher", "LY_Car", "LY_Video")) {
            Path original =
                    root.resolve(
                            ".local/profiles/"
                                    + TestProfiles.declaring(
                                            root, PassionServiceForegroundGuard.NAME)
                                    + "/originals/"
                                    + role
                                    + ".apk");
            byte[] raw = dex(original, role.equals("LY_Video") ? "classes2.dex" : "classes3.dex");
            byte[] input =
                    role.equals("LY_Launcher")
                            ? PassionSidebarGuard.apply(TestProfiles.SYNTHETIC, 30, original, raw)
                            : role.equals("LY_Car")
                                    ? PassionVehicleAudioGuard.apply(
                                            TestProfiles.SYNTHETIC, 30, original, raw)
                                    : PassionServiceForegroundGuard.apply(
                                            TestProfiles.SYNTHETIC, 30, original, raw);
            // Production global preflight sees RAW entries; actual recipe application sees prior
            // transforms.
            PassionBluetoothVolumeGuard.validateOriginalChain(
                    TestProfiles.SYNTHETIC, 30, original, raw);
            reject(() ->
                    PassionBluetoothVolumeGuard.validateOriginalChain(
                            TestProfiles.SYNTHETIC, 30, original, input));
            negatives++;
            var before = PassionBluetoothVolumeGuard.classes(input);
            byte[] output =
                    PassionBluetoothVolumeGuard.apply(TestProfiles.SYNTHETIC, 30, original, input);
            var after = PassionBluetoothVolumeGuard.classes(output);

            for (var c : before.values()) {
                for (var m : c.getMethods()) {
                    var t = PassionBluetoothVolumeGuard.target(m);

                    if (t == null) {
                        continue;
                    }

                    methods++;
                    calls += t.calls();
                    org.jf.dexlib2.iface.Method transformed = null;

                    for (var candidate : after.get(c.getType()).getMethods()) {
                        if (candidate.getName().equals(m.getName())) {
                            transformed = candidate;
                        }
                    }

                    check(transformed != null);
                    PassionBluetoothVolumeGuard.validateAfter(m, transformed);
                    // A wrong receiver/argument register must be rejected even when inverse removal
                    // could hide it.
                    var body = new MutableMethodImplementation(transformed.getImplementation());
                    boolean mutated = false;

                    for (int i = 0; i < body.getInstructions().size(); i++) {
                        var in = body.getInstructions().get(i);

                        if (in instanceof ReferenceInstruction ref
                                && in instanceof FiveRegisterInstruction r
                                && ref.getReference()
                                        .toString()
                                        .startsWith(PassionBluetoothVolumeGuard.HELPER + "->")) {
                            body.replaceInstruction(
                                    i,
                                    new BuilderInstruction35c(
                                            Opcode.INVOKE_STATIC,
                                            r.getRegisterCount(),
                                            (r.getRegisterC() + 1) % t.registers(),
                                            r.getRegisterD(),
                                            r.getRegisterE(),
                                            r.getRegisterF(),
                                            r.getRegisterG(),
                                            ref.getReference()));
                            mutated = true;
                            break;
                        }
                    }

                    check(mutated);
                    var bad = PassionServiceForegroundGuard.copy(transformed, body);
                    reject(() -> PassionBluetoothVolumeGuard.validateAfter(m, bad));
                    negatives++;
                }
            }

            reject(() ->
                    PassionBluetoothVolumeGuard.apply(TestProfiles.SYNTHETIC, 28, original, input));
            negatives++;
            reject(() ->
                    PassionBluetoothVolumeGuard.apply(
                            TestProfiles.SYNTHETIC, 30, original, output));
            negatives++;
            Files.write(stage.resolve(role + ".dex"), output);
        }

        check(methods == 12 && calls == 20);
        Files.writeString(
                stage.resolve("receipt.json"),
                Json.write(
                                Map.of(
                                        "methods",
                                        methods,
                                        "SDK_invocations",
                                        calls,
                                        "negative_checks",
                                        negatives,
                                        "all_class_inverse",
                                        true,
                                        "exact_prior_input_chain",
                                        true,
                                        "supported_streams_preserved_by_helper_fixture",
                                        true,
                                        "ART_verified",
                                        false,
                                        "real_guest",
                                        false))
                        + "\n");
        System.out.println(
                "12 methods/20 SDK invokes; "
                        + negatives
                        + " negatives; prepared-chain/inverse PASS; ART/runtime pending");
    }
}
