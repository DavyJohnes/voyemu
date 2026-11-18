package com.voyemu.prepare;

import java.nio.file.Path;
import java.util.List;
import java.util.Map;

/** Source registry and recipe guards complement the independently executed helper behavior test. */
public final class NxpMusicMediaMetadataTest {
    static void check(boolean value, String message) {
        if (!value) {
            throw new AssertionError(message);
        }
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        String id = "free-09-chinese-20221230-nxp-rc45", name = "nxp-music-media-compat.json";
        var profile = new ProfileRegistry(root, id).get(id);
        var exported = ProfileRegistry.patchMetadata(profile);

        check(exported.containsKey(name), "Production profile must declare the Music recipe");
        var metadata = Json.object(exported.get(name));
        check(
                Recipes.hasRegisteredCoverage(name, metadata),
                "Production handler revision must match exported source metadata");
        Path recipe = root.resolve("profiles/" + id + "/patches/" + name);
        check(
                FilesSupport.hash(recipe).equals(Json.str(metadata, "sourceSha256")),
                "Exported recipe source pin differs");
        Recipes.Target target =
                new Recipes.Target() {
                    @Override
                    public String profile() {
                        return id;
                    }

                    @Override
                    public int api() {
                        return 28;
                    }

                    @Override
                    public String abi() {
                        return "arm64-v8a";
                    }

                    @Override
                    public String originalSha256() {
                        return "0".repeat(64);
                    }

                    @Override
                    public Path output() {
                        return null;
                    }

                    @Override
                    public Path work() {
                        return Path.of(args[1]);
                    }

                    @Override
                    public Map<String, Path> uses() {
                        return Map.of();
                    }

                    @Override
                    public void addDex(String source) {
                        throw new AssertionError("Unpinned donor reached mutation");
                    }

                    @Override
                    public void putEntry(Path source, String name) {
                        throw new AssertionError();
                    }

                    @Override
                    public void removeEntry(String name) {
                        throw new AssertionError();
                    }

                    @Override
                    public void useLibraries(List<String> names) {
                        throw new AssertionError();
                    }
                };
        boolean refused = false;

        try {
            Recipes.apply(name, metadata, target);
        } catch (Failure expected) {
            refused = true;
        }

        check(refused, "Production dispatcher accepted an unpinned Music donor");
        System.out.println(
                "PASS source Music registry, exact recipe export and production donor"
                        + " guard; OEM DEX integration is explicit");
    }
}
