package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;

public final class ProfileRegistryIsolationTest {
    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]).toRealPath();
        Path selected = root.resolve("profiles/selected");

        Files.createDirectories(selected);
        Files.writeString(selected.resolve("profile.json"), "{\"schema\":1}");
        Files.writeString(selected.resolve("component.java"), "initial");
        Files.writeString(
                root.resolve("profiles/unrelated-invalid-file"), "other profile authoring");
        ProfileRegistry registry = new ProfileRegistry(root, "selected");
        var profile = registry.get("selected");
        Files.writeString(root.resolve("profiles/unrelated-invalid-file"), "updated");
        registry.verifySource(profile);
        Files.writeString(selected.resolve("component.java"), "changed");
        boolean rejected = false;

        try {
            registry.verifySource(profile);
        } catch (Failure expected) {
            rejected = true;
        }

        if (!rejected) {
            throw new AssertionError("selected source corruption accepted");
        }

        System.out.println("Selected profile isolated; selected source edit rejected");
    }
}
