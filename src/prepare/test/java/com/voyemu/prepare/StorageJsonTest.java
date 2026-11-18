package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Map;

public final class StorageJsonTest {
    static void check(boolean condition) {
        if (!condition) {
            throw new AssertionError();
        }
    }

    static void rejected(Runnable action) {
        try {
            action.run();
        } catch (IllegalArgumentException | Failure expected) {
            return;
        }

        throw new AssertionError("Expected rejection");
    }

    public static void main(String[] args) throws Exception {
        Object sample =
                Json.parse(
                        "{\"name\":\"Приборка"
                                + " \\uD83D\\uDE97\",\"n\":-1.5e2,\"items\":[true,null,42]}");

        check(Json.write(Json.parse(Json.write(sample))).equals(Json.write(sample)));

        for (String bad :
                new String[] {
                    "{\"x\":1,\"x\":2}",
                    "[1,]",
                    "{\"x\":}",
                    "01",
                    "1.",
                    "true false",
                    "\"\\x\"",
                    "[".repeat(130)
                }) {
            rejected(() -> Json.parse(bad));
        }

        Path temp = Files.createTempDirectory("voyemu-native-storage-test-").toRealPath();

        try {
            Storage mac = Storage.resolve(null, Map.of(), "mac os x", temp);
            check(mac.data().equals(temp.resolve("Library/Application Support/voyemu/data")));
            check(!Files.exists(mac.data()));
            Storage portable =
                    Storage.resolve(
                            temp.resolve("home").toString(),
                            Map.of("VOYEMU_HOME", "/ignored"),
                            "mac",
                            temp);
            check(portable.data().equals(temp.resolve("home/data")));
            rejected(() -> Storage.resolve("relative", Map.of(), "mac", temp));
            Storage linux =
                    Storage.resolve(null, Map.of("XDG_DATA_HOME", "relative"), "linux", temp);
            check(linux.data().equals(temp.resolve(".local/share/voyemu")));
            rejected(() -> Storage.confined(temp, "../escape"));
            Path link = temp.resolve("link");
            Files.createSymbolicLink(link, temp);
            rejected(() -> Storage.confined(temp, "link/file"));
            rejected(() -> Storage.confined(link.resolve("managed"), "file"));
            Files.delete(link);
            Storage.atomic(temp.resolve("value.json"), Json.write(sample) + "\n");
            check(Json.parse(Files.readString(temp.resolve("value.json"))).equals(sample));
        } finally {
            try (var paths = Files.walk(temp)) {
                for (Path path : paths.sorted(java.util.Comparator.reverseOrder()).toList()) {
                    Files.delete(path);
                }
            }
        }

        System.out.println("Storage and JSON guards passed");
    }
}
