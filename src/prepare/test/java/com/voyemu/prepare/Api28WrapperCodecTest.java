package com.voyemu.prepare;

import java.nio.file.Path;

/**
 * A copied local A9 profile consumes exactly the stable recipe/output, irrespective of its name.
 */
public final class Api28WrapperCodecTest {
    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]);
        var base =
                new ProfileRegistry(root, Api28CodecNativeFile.PROFILE)
                        .get(Api28CodecNativeFile.PROFILE);
        String name = Api28CodecNativeFile.PROFILE + "-copy";
        var data = new java.util.LinkedHashMap<String, Object>(base.data());

        data.put("id", name);
        data.put("capture", java.util.Map.of("profile", name, "referenceProfile", base.id()));
        var wrapper =
                new ProfileRegistry.Profile(name, base.revision(), data, base.resourcePrefix());

        if (!base.referenceProfile().equals(wrapper.referenceProfile())) {
            throw new AssertionError("different recipe families");
        }

        if (!Api28CodecNativeFile.output(base).equals(Api28CodecNativeFile.output(wrapper))) {
            throw new AssertionError("copied wrapper selects obsolete codec output");
        }

        System.out.println("PASS copied stable wrapper codec recipe/output parity");
    }
}
