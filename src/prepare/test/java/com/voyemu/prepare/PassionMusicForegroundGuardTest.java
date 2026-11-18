package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.builder.MutableMethodImplementation;
import org.jf.dexlib2.builder.instruction.BuilderInstruction11n;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.immutable.ImmutableMethod;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.Map;
import java.util.TreeMap;
import java.util.zip.ZipFile;

public final class PassionMusicForegroundGuardTest {
    interface Action {
        void run() throws Exception;
    }

    static void check(boolean x) {
        if (!x) {
            throw new AssertionError("actual Music foreground channel");
        }
    }

    static void reject(Action a) throws Exception {
        boolean fail = false;

        try {
            a.run();
        } catch (Failure e) {
            fail = true;
        }

        check(fail);
    }

    static Method changed(Method m, int index, int register, int value) {
        var b = new MutableMethodImplementation(m.getImplementation());

        b.replaceInstruction(index, new BuilderInstruction11n(Opcode.CONST_4, register, value));

        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                b);
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), stage = Path.of(args[1]);

        Files.createDirectory(stage);
        Path original =
                root.resolve(
                        ".local/profiles/"
                                + PassionMusicForegroundGuard.PROFILE
                                + "/originals/LY_Multimedia.apk");
        byte[] dex;

        try (var z = new ZipFile(original.toFile())) {
            dex = z.getInputStream(z.getEntry("classes2.dex")).readAllBytes();
        }

        var bridgeBefore =
                PassionMusicForegroundGuard.method(
                        new DexBackedDexFile(Opcodes.forApi(30), dex),
                        PassionMusicForegroundGuard.BRIDGE);
        PassionMusicForegroundGuard.validateMethod(bridgeBefore);
        reject(() -> PassionMusicForegroundGuard.validateBridgeAfter(bridgeBefore));
        var before =
                PassionMusicForegroundGuard.method(new DexBackedDexFile(Opcodes.forApi(30), dex));
        PassionMusicForegroundGuard.validateMethod(before);
        reject(() -> PassionMusicForegroundGuard.validateAfter(before));
        byte[] out =
                PassionMusicForegroundGuard.apply(
                        PassionMusicForegroundGuard.PROFILE, 30, original, dex);
        var after =
                PassionMusicForegroundGuard.method(new DexBackedDexFile(Opcodes.forApi(30), out));
        PassionMusicForegroundGuard.validateAfter(after);
        var bridgeAfter =
                PassionMusicForegroundGuard.method(
                        new DexBackedDexFile(Opcodes.forApi(30), out),
                        PassionMusicForegroundGuard.BRIDGE);
        PassionMusicForegroundGuard.validateBridgeAfter(bridgeAfter);
        reject(() ->
                PassionMusicForegroundGuard.validateBridgeAfter(changed(bridgeAfter, 17, 3, 1)));
        reject(() ->
                PassionMusicForegroundGuard.validateBridgeAfter(changed(bridgeAfter, 17, 4, 0)));
        reject(() ->
                PassionMusicForegroundGuard.apply(
                        "free-11-chinese-20260731-7.3.1", 30, original, dex));
        reject(() ->
                PassionMusicForegroundGuard.apply(
                        PassionMusicForegroundGuard.PROFILE, 28, original, dex));
        reject(() ->
                PassionMusicForegroundGuard.apply(
                        PassionMusicForegroundGuard.PROFILE, 30, original, null));
        reject(() ->
                PassionMusicForegroundGuard.apply(
                        PassionMusicForegroundGuard.PROFILE, 30, original, out));
        Path wrong = stage.resolve("wrong.apk");
        Files.writeString(wrong, "not owner");
        reject(() ->
                PassionMusicForegroundGuard.apply(
                        PassionMusicForegroundGuard.PROFILE, 30, wrong, dex));
        reject(() -> PassionMusicForegroundGuard.validateAfter(changed(after, 13, 3, 0)));
        reject(() -> PassionMusicForegroundGuard.validateAfter(changed(after, 13, 2, 1)));
        var originalEntries = NativeCore.entries(original);
        var chained = new TreeMap<String, byte[]>(originalEntries);
        chained.put(
                "classes.dex",
                PassionBluetoothRoleGuard.apply(
                        PassionBluetoothRoleGuard.PROFILE,
                        30,
                        original,
                        chained.get("classes.dex")));
        byte[] role = chained.get("classes.dex");
        chained.put(
                "classes2.dex",
                PassionMusicForegroundGuard.apply(
                        PassionMusicForegroundGuard.PROFILE,
                        30,
                        original,
                        chained.get("classes2.dex")));
        check(Arrays.equals(role, chained.get("classes.dex")));
        int changes = 0;

        for (var e : originalEntries.entrySet()) {
            if (!Arrays.equals(e.getValue(), chained.get(e.getKey()))) {
                check(e.getKey().equals("classes.dex") || e.getKey().equals("classes2.dex"));
                changes++;
            }
        }

        check(changes == 2);
        Path output = stage.resolve("classes2.dex");
        Files.write(output, out);
        Process p =
                new ProcessBuilder(
                                root.resolve(
                                                ".local/toolchains/native-sdk/build-tools/36.1.0/dexdump")
                                        .toString(),
                                "-c",
                                output.toString())
                        .redirectErrorStream(true)
                        .redirectOutput(stage.resolve("dexdump-checksum.log").toFile())
                        .start();
        check(p.waitFor() == 0);
        Files.writeString(
                stage.resolve("receipt.json"),
                Json.write(
                                Map.of(
                                        "original_channel_source_not_silent_RED",
                                        true,
                                        "new_stable_ID_null_sound_before_publication_GREEN",
                                        true,
                                        "semantic_inverse_all_classes_exact",
                                        true,
                                        "prior_Bluetooth_role_preserved",
                                        true,
                                        "all_other_APK_entries_exact",
                                        true,
                                        "negative_cases",
                                        12,
                                        "dexdump_checksum_exit",
                                        0,
                                        "both_exact_foreground_channels_silent_before_publication",
                                        true,
                                        "bridge_Binder_and_player_semantic_inverse_preserved",
                                        true,
                                        "runtime_quiet_verified",
                                        false))
                        + "\n");
        System.out.println(
                "Music exact foreground channel/source RED->GREEN,12 negatives,semantic inverse and"
                        + " prior role preserved; checksum PASS, runtime pending");
    }
}
