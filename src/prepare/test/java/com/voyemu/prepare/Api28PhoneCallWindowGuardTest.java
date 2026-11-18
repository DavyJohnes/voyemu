package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Map;
import java.util.Objects;
import java.util.TreeMap;

/** Real pinned donor and exact prepared bytes; no hand-patched fixture. */
public final class Api28PhoneCallWindowGuardTest {
    interface Check {
        void run() throws Exception;
    }

    static void reject(Check check) throws Exception {
        try {
            check.run();
        } catch (Failure expected) {
            return;
        }

        throw new AssertionError("unexpected input accepted");
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), stage = Path.of(args[1]);

        Files.createDirectories(stage);
        Path apk =
                root.resolve(
                        ".local/profiles/"
                                + Api28PhoneCallWindowGuard.PROFILE
                                + "/originals/BluetoothPhone.apk");
        byte[] original = NativeCore.entries(apk).get("classes.dex");
        byte[] changed =
                Api28PhoneCallWindowGuard.apply(
                        Api28PhoneCallWindowGuard.PROFILE, 28, apk, original);
        Api28PhoneCallWindowGuard.verify(original, changed);

        if (!Arrays.equals(
                changed,
                Api28PhoneCallWindowGuard.apply(
                        Api28PhoneCallWindowGuard.PROFILE, 28, apk, original))) {
            throw new AssertionError("nondeterministic");
        }

        reject(() -> Api28PhoneCallWindowGuard.apply("foreign", 28, apk, original));
        reject(() ->
                Api28PhoneCallWindowGuard.apply(
                        Api28PhoneCallWindowGuard.PROFILE, 30, apk, original));
        reject(() ->
                Api28PhoneCallWindowGuard.apply(
                        Api28PhoneCallWindowGuard.PROFILE, 28, apk, changed));
        byte[] unknown = original.clone();
        unknown[unknown.length - 1] ^= 1;
        reject(() ->
                Api28PhoneCallWindowGuard.apply(
                        Api28PhoneCallWindowGuard.PROFILE, 28, apk, unknown));
        reject(() -> Api28PhoneCallWindowGuard.verify(original, original));
        byte[] drift = changed.clone();
        drift[drift.length - 1] ^= 1;
        reject(() -> Api28PhoneCallWindowGuard.verify(original, drift));
        reject(() -> Api28PhoneCallWindowGuard.verify(unknown, changed));
        Path wrong = stage.resolve("wrong-original.apk");
        Files.write(wrong, new byte[] {1});
        reject(() ->
                Api28PhoneCallWindowGuard.apply(
                        Api28PhoneCallWindowGuard.PROFILE, 28, wrong, original));
        var before = new DexBackedDexFile(Opcodes.forApi(28), original);
        var after = new DexBackedDexFile(Opcodes.forApi(28), changed);
        var methods = new TreeMap<String, String>();

        for (var type : before.getClasses()) {
            for (var method : type.getMethods()) {
                methods.put(method.toString(), FrameworkDexMaterializer.semantic(method));
            }
        }

        int equal = 0, delta = 0;

        for (var type : after.getClasses()) {
            for (var method : type.getMethods()) {
                if (Objects.equals(
                        methods.get(method.toString()),
                        FrameworkDexMaterializer.semantic(method))) {
                    equal++;
                } else {
                    if (!Api28PhoneCallWindowGuard.METHOD.equals(method.toString())) {
                        throw new AssertionError("extra changed method " + method);
                    }

                    delta++;
                }
            }
        }

        if (equal != 51798 || delta != 1) {
            throw new AssertionError("method inventory " + equal + "/" + delta);
        }

        Files.write(stage.resolve("classes.dex"), changed);
        Files.writeString(
                stage.resolve("guard-result.json"),
                Json.write(
                                Map.of(
                                        "original_dex_sha256",
                                        NativeCore.hash(original),
                                        "prepared_dex_sha256",
                                        NativeCore.hash(changed),
                                        "unchanged_original_methods",
                                        equal,
                                        "approved_changed_methods",
                                        delta,
                                        "negative_cases",
                                        8,
                                        "same_original_deterministic",
                                        true,
                                        "second_apply_rejected",
                                        true))
                        + "\n");
        System.out.println(
                "PASS exact guard output: 51798 unchanged, 1 approved method; 8 negatives;"
                        + " deterministic; "
                        + stage.resolve("classes.dex"));
    }
}
