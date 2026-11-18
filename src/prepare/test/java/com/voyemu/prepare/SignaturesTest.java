package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Map;
import java.util.Set;

public final class SignaturesTest {
    public static void main(String[] args) throws Exception {
        Path platform = Path.of(args[0]);
        Path project =
                Path.of(System.getProperty("voyemu.project.root", System.getProperty("user.dir")))
                        .toRealPath();

        if (!Files.isDirectory(project.resolve("src/prepare"))) {
            throw new AssertionError("Voyemu project root required");
        }

        Path parent = project.resolve(".local/build/tmp");
        Storage.directory(parent);
        Path root = Files.createTempDirectory(parent, "voyemu-native-donor-test-").toRealPath();

        try {
            Path fixture = root.resolve("fixture");
            Files.createDirectories(fixture.resolve("own"));
            Files.writeString(
                    fixture.resolve("own/Donor.java"),
                    """
                    package own;

                    public class Donor {
                        public static final int ANSWER = 42;
                        public static final Object NOTHING = null;
                        public static final boolean FLAG = false;
                        public Object object;
                        public int count;

                        public static class Inner {
                            public int read() {
                                return 1;
                            }
                        }
                    }
                    """);
            Path originalDex =
                    DexOperations.javaDex(
                                    fixture,
                                    null,
                                    List.of(),
                                    platform,
                                    root.resolve("fixture-dex"),
                                    28)
                            .resolve("classes.dex");
            StorageJsonTest.check(
                    NativeCore.classes(
                                    org.jf.dexlib2.DexFileFactory.loadDexFile(
                                            originalDex.toFile(),
                                            org.jf.dexlib2.Opcodes.forApi(28)))
                            .equals(Set.of("Lown/Donor;", "Lown/Donor$Inner;")));
            Path apk = root.resolve("own-fixture.apk");
            NativeCore.zip(apk, Map.of("classes.dex", Files.readAllBytes(originalDex)), false);
            var donor =
                    DexOperations.donor(
                            apk,
                            Map.of("dex", "classes.dex", "classes", List.of("own.Donor")),
                            root.resolve("donor"),
                            28);
            StorageJsonTest.check(donor.descriptors().size() == 2);
            Path source = root.resolve("source");
            Files.createDirectory(source);
            Files.writeString(
                    source.resolve("Own.java"),
                    "package own; public class Own { public int read(Donor.Inner value) { return"
                            + " Donor.ANSWER + value.read(); } }\n");
            Path dex =
                    DexOperations.javaDex(
                            source,
                            null,
                            List.of(donor.signatures()),
                            platform,
                            root.resolve("compile"),
                            28);
            Set<String> own =
                    NativeCore.classes(
                            org.jf.dexlib2.DexFileFactory.loadDexFile(
                                    dex.resolve("classes.dex").toFile(),
                                    org.jf.dexlib2.Opcodes.forApi(28)));
            StorageJsonTest.check(own.equals(Set.of("Lown/Own;")));
            StorageJsonTest.check(
                    NativeCore.classes(
                                    org.jf.dexlib2.DexFileFactory.loadDexFile(
                                            donor.dex().toFile(),
                                            org.jf.dexlib2.Opcodes.forApi(28)))
                            .equals(donor.descriptors()));
        } finally {
            FilesSupport.removeOwnedTree(root);
        }

        System.out.println(
                "Donor metadata signatures, inner classes and compile-only exclusion passed");
    }
}
