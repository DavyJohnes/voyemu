package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.List;
import java.util.Set;

/** Real javac/D8 output must contain only our provider, with OEM ABI accessed reflectively. */
public final class PhoneDexTest {
    public static void main(String[] args) throws Exception {
        Path dex =
                DexOperations.javaDex(
                        Path.of(args[0]), null, List.of(), Path.of(args[1]), Path.of(args[2]), 28);
        Set<String> actual =
                NativeCore.classes(
                        org.jf.dexlib2.DexFileFactory.loadDexFile(
                                dex.resolve("classes.dex").toFile(),
                                org.jf.dexlib2.Opcodes.forApi(28)));
        Set<String> owners = new java.util.TreeSet<>();

        for (String name : actual) {
            owners.add(name.replaceFirst("\\$.*", ";"));
        }

        if (!owners.equals(
                Set.of(
                        "Lcom/voyemu/input/PhoneControlProvider;",
                        "Lcom/voyemu/input/PhonePeerProvider;"))) {
            throw new AssertionError(actual);
        }

        System.out.println("PASS real javac/D8 API28 provider-only DEX: " + actual);
    }
}
