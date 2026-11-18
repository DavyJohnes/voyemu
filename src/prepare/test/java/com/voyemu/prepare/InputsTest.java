package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;

public final class InputsTest {
    public static void main(String[] args) throws Exception {
        Path root = Files.createTempDirectory("voyemu-native-input-test-").toRealPath();

        try {
            Path owner = root.resolve("owner");
            Files.createDirectory(owner);
            Files.writeString(owner.resolve("own.txt"), "own data");
            String pin = FilesSupport.hash(owner.resolve("own.txt"));
            var profile =
                    new ProfileRegistry.Profile(
                            "own-test",
                            "own-recipe",
                            Map.of(
                                    "original",
                                    Map.of("own", Map.of("file", "own.txt", "sha256", pin))),
                            "unused");
            Storage storage =
                    Storage.resolve(root.resolve("home").toString(), Map.of(), "mac", root);
            StorageJsonTest.check(
                    !Inputs.check(profile, storage).ok() && !Files.exists(storage.data()));
            Arguments fetch =
                    Arguments.parse(
                            new String[] {"fetch", "own-test", "--input", owner.toString()});
            Map<String, Object> result = Inputs.fetch(profile, storage, fetch, null, message -> {});
            StorageJsonTest.check(
                    Boolean.TRUE.equals(result.get("changed"))
                            && Inputs.check(profile, storage).ok());
            StorageJsonTest.check(Files.readString(owner.resolve("own.txt")).equals("own data"));
            StorageJsonTest.check(
                    Boolean.FALSE.equals(
                            Inputs.fetch(profile, storage, fetch, null, message -> {})
                                    .get("changed")));
            Files.writeString(owner.resolve("own.txt"), "different data");
            Storage newStorage =
                    Storage.resolve(root.resolve("second").toString(), Map.of(), "mac", root);

            try {
                Inputs.fetch(profile, newStorage, fetch, null, message -> {});
                throw new AssertionError("Mismatch accepted");
            } catch (Failure expected) {
                if (!expected.code.equals("ORIGINAL_HASH_MISMATCH")) {
                    throw expected;
                }
            }

            StorageJsonTest.check(!Files.exists(newStorage.originals("own-test")));
            StorageJsonTest.check(Inputs.check(profile, storage).ok());
            Arguments force =
                    Arguments.parse(
                            new String[] {
                                "fetch", "own-test", "--input", owner.toString(), "--force"
                            });
            Inputs.fetch(profile, storage, force, null, message -> {});
            StorageJsonTest.check(Inputs.check(profile, storage).ok());
            StorageJsonTest.check(
                    Json.obj(profile.data(), "original")
                            .get("own")
                            .equals(Map.of("file", "own.txt", "sha256", pin)));
        } finally {
            FilesSupport.removeOwnedTree(root);
        }

        System.out.println("Owner input atomic import and local revision guards passed");
    }
}
