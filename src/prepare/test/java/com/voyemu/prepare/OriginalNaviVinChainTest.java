package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipFile;

public final class OriginalNaviVinChainTest {
    static void check(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    public static void main(String[] a) throws Exception {
        Path jar = Path.of(a[0]);
        byte[] original;

        try (ZipFile z = new ZipFile(jar.toFile())) {
            original = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }

        String profile = "free-11-chinese-20260731-7.3.1";
        OriginalNaviVinGuard.validateInput(profile, 30, "arm64-v8a", jar, original);
        byte[] predecessor =
                AudioPolicyConsumerGuard.apply(
                        TestProfiles.constants(
                                Path.of("").toAbsolutePath(), "api30-audio-consumer", jar),
                        30,
                        "arm64-v8a",
                        jar,
                        original);
        OriginalNaviVinGuard.validateInput(profile, 30, "arm64-v8a", jar, predecessor);
        Map<String, List<String>> members = new HashMap<>();
        List<String> before = new ArrayList<>(), after = new ArrayList<>();

        for (ClassDef c : new DexBackedDexFile(Opcodes.forApi(30), original).getClasses()) {
            before.add(c.getType());
            check(members.put(c.getType(), HiddenApiOffsets.members(c)) == null);
        }

        for (ClassDef c : new DexBackedDexFile(Opcodes.forApi(30), predecessor).getClasses()) {
            after.add(c.getType());
            check(
                    members.containsKey(c.getType())
                            && members.get(c.getType()).equals(HiddenApiOffsets.members(c)));
        }

        check(before.size() == after.size() && new HashSet<>(before).equals(new HashSet<>(after)));
        check(
                HiddenApiOffsets.rawFlags(original, before, members)
                        .equals(HiddenApiOffsets.rawFlags(predecessor, after, members)));
        byte[] result = OriginalNaviVinGuard.apply(profile, 30, "arm64-v8a", jar, predecessor);
        check(!NativeCore.hash(result).equals(NativeCore.hash(predecessor)));
        System.out.println(
                "PASS original validation, audio predecessor exact raw member flags, VIN guard and"
                        + " reversed-order rejection");
    }
}
