package com.voyemu.prepare;

import org.jf.dexlib2.DexFileFactory;
import org.jf.dexlib2.Opcodes;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;

public final class JavaDexCompileOnlyTest {
    private static Set<String> inventory(Path output) throws Exception {
        Set<String> names = new TreeSet<>();

        for (Path file : FilesSupport.tree(output).values()) {
            names.addAll(
                    NativeCore.classes(
                            DexFileFactory.loadDexFile(file.toFile(), Opcodes.forApi(28))));
        }

        return names;
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]), platform = Path.of(args[1]), work = Path.of(args[2]);
        Path source = work.resolve("fixture/src"), declarations = source.resolve("abi");

        Files.createDirectories(declarations);
        Files.writeString(
                declarations.resolve("Contract.java"),
                "package fixture; public class Contract { public int value() { return 3; } }");
        Files.writeString(
                source.resolve("Consumer.java"),
                "package fixture; public class Consumer { public int call(Contract c) { return"
                        + " c.value(); } }");
        Set<String> own =
                inventory(
                        DexOperations.javaDex(
                                source,
                                null,
                                List.of(),
                                platform,
                                work.resolve("separate"),
                                28,
                                declarations));

        if (!own.equals(Set.of("Lfixture/Consumer;"))) {
            throw new AssertionError(own);
        }

        Set<String> legacy =
                inventory(
                        DexOperations.javaDex(
                                source, null, List.of(), platform, work.resolve("legacy"), 28));

        if (!legacy.equals(Set.of("Lfixture/Consumer;", "Lfixture/Contract;"))) {
            throw new AssertionError(legacy);
        }

        try {
            DexOperations.javaDex(
                    source, null, List.of(), platform, work.resolve("invalid"), 28, work);
            throw new AssertionError("Outside declarations accepted");
        } catch (Failure expected) {
            if (!expected.getMessage().contains("nested source")) {
                throw expected;
            }
        }

        Path bluetooth = root.resolve("profiles/common/components/bluetooth/src");
        Set<String> bluetoothNames =
                inventory(
                        DexOperations.javaDex(
                                bluetooth,
                                null,
                                List.of(),
                                platform,
                                work.resolve("bluetooth"),
                                28,
                                bluetooth.resolve("stub-cls")));

        if (bluetoothNames.stream()
                .anyMatch(name ->
                        !name.matches(
                                "Lcom/qinggan/bluetooth/internal/ivt8/[A-Za-z0-9]+(?:\\$[^;]+)?;"))) {
            throw new AssertionError(bluetoothNames);
        }

        if (bluetoothNames.stream().noneMatch(name -> name.endsWith("/SyntheticPhonePeer;"))
                || bluetoothNames.stream().noneMatch(name -> name.endsWith("/Ivt8BTAdapter;"))) {
            throw new AssertionError(bluetoothNames);
        }

        System.out.println(
                "PASS explicit compile-only ABI, unchanged default packaging, confinement, actual"
                        + " A9 DEX inventory: "
                        + bluetoothNames);
    }
}
