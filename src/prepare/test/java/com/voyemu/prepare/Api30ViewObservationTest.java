package com.voyemu.prepare;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;

public final class Api30ViewObservationTest {
    private static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    private static int integer(byte[] bytes, int at) {
        return java.nio.ByteBuffer.wrap(bytes).order(java.nio.ByteOrder.LITTLE_ENDIAN).getInt(at);
    }

    private static int unsigned(byte[] bytes, int[] at) {
        int value = 0, shift = 0;

        for (; ; ) {
            int b = bytes[at[0]++] & 255;
            value |= (b & 127) << shift;

            if ((b & 128) == 0) {
                return value;
            }

            shift += 7;
            check(shift < 35);
        }
    }

    // Read encoded restriction bytes and member index deltas directly, without class/member
    // iterators.
    private static java.util.Map<String, Integer> restrictions(byte[] bytes) {
        var dex =
                new org.jf.dexlib2.dexbacked.DexBackedDexFile(
                        org.jf.dexlib2.Opcodes.forApi(30), bytes);
        int map = integer(bytes, 52), hidden = 0;

        for (int i = 0; i < integer(bytes, map); i++) {
            int item = map + 4 + i * 12;

            if ((bytes[item] & 255) == 0 && (bytes[item + 1] & 255) == 240) {
                hidden = integer(bytes, item + 8);
            }
        }

        check(hidden != 0);
        var result = new java.util.TreeMap<String, Integer>();
        int classes = integer(bytes, 96), definitions = integer(bytes, 100);

        for (int i = 0; i < classes; i++) {
            int data = integer(bytes, definitions + i * 32 + 24);

            if (data == 0) {
                continue;
            }

            int[] at = {data};
            int[] counts = new int[4];

            for (int group = 0; group < 4; group++) {
                counts[group] = unsigned(bytes, at);
            }

            int offset = integer(bytes, hidden + 4 + i * 4);
            int[] restriction = {hidden + offset};

            for (int group = 0; group < 4; group++) {
                int index = 0;

                for (int member = 0; member < counts[group]; member++) {
                    index += unsigned(bytes, at);
                    unsigned(bytes, at);

                    if (group >= 2) {
                        unsigned(bytes, at);
                    }

                    String key =
                            group < 2
                                    ? dex.getFieldSection().get(index).toString()
                                    : dex.getMethodSection().get(index).toString();
                    check(result.put(key, offset == 0 ? 0 : unsigned(bytes, restriction)) == null);
                }
            }
        }

        return result;
    }

    public static void main(String[] args) throws Exception {
        Path root = Path.of(args[0]),
                original = Path.of(args[1]),
                helper = Path.of(args[2]),
                output = Path.of(args[3]);

        for (String owner :
                new String[] {
                    Api30ViewObservation.PROFILE, "passion-11-russian-20260403-2.1.4.80"
                }) {
            checkOwnerSource(root, output, owner);
        }

        byte[] raw = NativeCore.entries(original).get("classes3.dex"),
                normalized = Api30ViewObservation.normalized(raw);
        check(Arrays.equals(Arrays.copyOf(raw, 8), Arrays.copyOf(normalized, 8)));
        check(
                Arrays.equals(
                        Arrays.copyOfRange(raw, 32, raw.length),
                        Arrays.copyOfRange(normalized, 32, normalized.length)));
        check(
                !Arrays.equals(
                        Arrays.copyOfRange(raw, 12, 32), Arrays.copyOfRange(normalized, 12, 32)));
        ViewDumpPatch.checkedDex(normalized, NativeCore.hash(normalized));

        try {
            ViewDumpPatch.checkedDex(raw, NativeCore.hash(raw));
            throw new AssertionError();
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }

        Api30ViewObservation.build(Api30ViewObservation.PROFILE, 30, original, helper, output);
        Path passionOutput = output.resolveSibling("passion-framework.jar");
        Api30ViewObservation.build(
                "passion-11-russian-20260403-2.1.4.80", 30, original, helper, passionOutput);
        var passionEntries = NativeCore.entries(passionOutput);
        var before = NativeCore.entries(original);
        var after = NativeCore.entries(output);
        check(passionEntries.keySet().equals(after.keySet()));

        for (String name : after.keySet()) {
            check(Arrays.equals(after.get(name), passionEntries.get(name)));
        }

        checkRejectedBuilds(original, helper, output);
        check(before.keySet().equals(after.keySet()));

        for (String name : before.keySet()) {
            if (!name.equals("classes3.dex")) {
                check(Arrays.equals(before.get(name), after.get(name)));
            }
        }

        byte[] donor =
                FrameworkBridgeVerifier.jumboHelper(NativeCore.entries(helper).get("classes.dex"));
        FrameworkBridgeVerifier.verify(
                before.get("classes3.dex"), donor, after.get("classes3.dex"));
        byte[] emitted = after.get("classes3.dex");
        int emittedMap = integer(emitted, 52);

        for (int i = 0; i < integer(emitted, emittedMap); i++) {
            int item = emittedMap + 4 + i * 12,
                    type = (emitted[item] & 255) | ((emitted[item + 1] & 255) << 8);

            if (type == 0xf000) {
                check((integer(emitted, item + 8) & 3) == 0);
            }
        }

        Api30HiddenApiMetadata.alignedTail(emitted, false);
        byte[] legacy =
                ViewDumpPatch.patch(
                        normalized, NativeCore.hash(normalized), donor, NativeCore.hash(donor), 30);
        int legacyMap = integer(legacy, 52),
                hiddenItem = legacyMap + 4 + (integer(legacy, legacyMap) - 2) * 12;
        int oldHidden = integer(legacy, hiddenItem + 8), alignmentPadding = -oldHidden & 3;

        if (alignmentPadding == 0) {
            check(Arrays.equals(legacy, Api30HiddenApiMetadata.alignedTail(legacy, false)));
        } else {
            try {
                Api30HiddenApiMetadata.alignedTail(legacy, false);
                throw new AssertionError();
            } catch (Failure expected) {
                // Rejection is the expected outcome.
            }
        }

        byte[] aligned = Api30HiddenApiMetadata.alignedTail(legacy, true);
        checkActualBadRepair(args, raw);

        int newHidden =
                integer(
                        aligned,
                        integer(aligned, 52)
                                + 4
                                + (integer(aligned, integer(aligned, 52)) - 2) * 12
                                + 8);
        checkAlignedPrefix(legacy, aligned, oldHidden, newHidden, alignmentPadding);
        checkInvalidLayouts(emitted, emittedMap, newHidden, alignmentPadding);
        checkMalformedRestore(raw, emitted);
        checkRestrictions(raw, after.get("classes3.dex"));

        checkRejectedOwners(original, helper, output);
        System.out.println(
                "Free A11 7.3.1 and Passion API30 exact framework bridge, pinned source, aligned"
                    + " hiddenAPI tail, zero padding/map bounds negatives, unchanged preceding data"
                    + " and ZIP entries, unrelated class semantics, independent raw restriction"
                    + " flags and fail-closed owner/API/repatch cases passed");
    }

    private static void checkOwnerSource(Path root, Path output, String owner) throws Exception {
        var profile =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve("profiles/" + owner + "/profile.json"))));

        check(Json.obj(profile, "components").containsKey("view-observation"));
        var component =
                Json.object(
                        Json.parse(
                                Files.readString(
                                        root.resolve(
                                                "profiles/"
                                                        + owner
                                                        + "/components/view-observation/component.json"))));
        check(
                Json.list(component.get("guestLibs"))
                        .equals(java.util.List.of("/system/framework/framework.jar")));
        var selected = new ProfileRegistry(root, owner).get(owner);
        check(
                Api30ViewObservation.source(selected, component)
                        .equals(root.resolve(Json.str(component, "api30ViewObservation"))));
        checkRejectedSources(root, output, owner, selected, component);

        String installedBridge =
                owner.equals(Api30ViewObservation.PROFILE)
                        ? "patched:original-navi-framework"
                        : "component:view-observation";
        check(
                Json.list(profile.get("install")).stream()
                        .map(Json::object)
                        .anyMatch(item ->
                                installedBridge.equals(item.get("artifact"))
                                        && "/system/framework/framework.jar"
                                                .equals(item.get("target"))));

        if (owner.equals(Api30ViewObservation.PROFILE)) {
            var naviFramework = Json.obj(Json.obj(profile, "patches"), "original-navi-framework");
            check(
                    "jar".equals(naviFramework.get("type"))
                            && "component:view-observation".equals(naviFramework.get("input")));
            check(
                    Json.list(naviFramework.get("steps"))
                            .equals(java.util.List.of("original-navi-egl.json")));
        }
    }

    private static void checkAlignedPrefix(
            byte[] legacy, byte[] aligned, int oldHidden, int newHidden, int alignmentPadding) {
        check(
                newHidden == oldHidden + alignmentPadding
                        && aligned.length == legacy.length + (alignmentPadding == 0 ? 0 : 4));
        check(
                Arrays.equals(
                        Arrays.copyOfRange(legacy, 112, oldHidden),
                        Arrays.copyOfRange(aligned, 112, oldHidden)));
        check(
                Arrays.equals(
                        Arrays.copyOfRange(
                                legacy, oldHidden, oldHidden + integer(legacy, oldHidden)),
                        Arrays.copyOfRange(
                                aligned, newHidden, newHidden + integer(aligned, newHidden))));

        for (int at = 32; at < 112; at++) {
            if (!((at >= 32 && at < 36) || (at >= 52 && at < 56) || (at >= 104 && at < 108))) {
                check(legacy[at] == aligned[at]);
            }
        }
    }

    private static void checkRejectedSources(
            Path root,
            Path output,
            String owner,
            ProfileRegistry.Profile selected,
            java.util.Map<String, Object> component)
            throws Exception {
        boolean rejectedSource;
        var badPath = new java.util.LinkedHashMap<>(component);

        badPath.put(
                "api30ViewObservation", "profiles/" + owner + "/components/view-observation/src");
        rejectedSource = false;

        try {
            Api30ViewObservation.source(selected, badPath);
        } catch (Failure expected) {
            rejectedSource = true;
        }

        check(rejectedSource);
        rejectedSource = false;

        try {
            Api30ViewObservation.source(
                    new ProfileRegistry.Profile(
                            "unsupported-profile",
                            selected.revision(),
                            selected.data(),
                            selected.resourcePrefix(),
                            root),
                    component);
        } catch (Failure expected) {
            rejectedSource = true;
        }

        check(rejectedSource);
        rejectedSource = false;

        try {
            Api30ViewObservation.source(
                    new ProfileRegistry.Profile(
                            selected.id(),
                            selected.revision(),
                            selected.data(),
                            selected.resourcePrefix()),
                    component);
        } catch (Failure expected) {
            rejectedSource = true;
        }

        check(rejectedSource);
        Path linkedRoot =
                Files.createTempDirectory(
                        output.toAbsolutePath().getParent(), "bridge-source-link-");

        try {
            Path link = linkedRoot.resolve(Json.str(component, "api30ViewObservation"));
            Files.createDirectories(link.getParent());
            Files.createSymbolicLink(link, Api30ViewObservation.source(selected, component));
            rejectedSource = false;

            try {
                Api30ViewObservation.source(
                        new ProfileRegistry.Profile(
                                selected.id(),
                                selected.revision(),
                                selected.data(),
                                selected.resourcePrefix(),
                                linkedRoot),
                        component);
            } catch (Failure expected) {
                rejectedSource = true;
            }

            check(rejectedSource);
        } finally {
            try (var files = Files.walk(linkedRoot)) {
                for (Path file : files.sorted(java.util.Comparator.reverseOrder()).toList()) {
                    Files.delete(file);
                }
            }
        }
    }

    private static void checkRejectedBuilds(Path original, Path helper, Path output)
            throws Exception {
        Path corruptedJar = output.resolveSibling("corrupted-framework.jar");
        byte[] corruptedJarBytes = Files.readAllBytes(original);

        corruptedJarBytes[corruptedJarBytes.length - 1] ^= 1;
        Files.write(corruptedJar, corruptedJarBytes);

        for (String owner :
                new String[] {
                    Api30ViewObservation.PROFILE, "passion-11-russian-20260403-2.1.4.80"
                }) {
            try {
                Api30ViewObservation.build(owner, 30, corruptedJar, helper, output);
                throw new AssertionError();
            } catch (Failure expected) {
                // Rejection is the expected outcome.
            }
        }

        for (String owner :
                new String[] {"unsupported-profile", "free-09-chinese-20260106-6.0.3"}) {
            try {
                Api30ViewObservation.build(owner, 30, original, helper, output);
                throw new AssertionError();
            } catch (Failure expected) {
                // Rejection is the expected outcome.
            }
        }
    }

    private static void checkActualBadRepair(String[] args, byte[] raw) throws Exception {
        if (args.length > 4) {
            byte[] actualBad = NativeCore.entries(Path.of(args[4])).get("classes3.dex");

            try {
                Api30HiddenApiMetadata.alignedTail(actualBad, false);
                throw new AssertionError();
            } catch (Failure expected) {
                // Rejection is the expected outcome.
            }

            byte[] repairedActual = Api30HiddenApiMetadata.restore(raw, actualBad);
            Api30HiddenApiMetadata.alignedTail(repairedActual, false);
            check(repairedActual.length == actualBad.length + 4);
            var actualFlags = restrictions(repairedActual);

            for (var entry : restrictions(raw).entrySet()) {
                check(entry.getValue().equals(actualFlags.get(entry.getKey())));
            }
        }
    }

    private static void checkInvalidLayouts(
            byte[] emitted, int emittedMap, int newHidden, int alignmentPadding) {
        java.util.List<byte[]> invalidLayouts = new java.util.ArrayList<>();

        if (alignmentPadding != 0) {
            byte[] badPadding = emitted.clone();
            badPadding[newHidden - 1] = 1;
            invalidLayouts.add(badPadding);
        }

        if (newHidden + integer(emitted, newHidden) < emittedMap) {
            byte[] badTailPadding = emitted.clone();
            badTailPadding[newHidden + integer(emitted, newHidden)] = 1;
            invalidLayouts.add(badTailPadding);
        }

        byte[] overlap = emitted.clone();
        java.nio.ByteBuffer.wrap(overlap)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN)
                .putInt(newHidden, emitted.length);
        invalidLayouts.add(overlap);
        byte[] outOfBounds = emitted.clone();
        java.nio.ByteBuffer.wrap(outOfBounds)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN)
                .putInt(emittedMap + 4 + 12 + 8, emitted.length);
        invalidLayouts.add(outOfBounds);
        byte[] unknownMap = emitted.clone();
        unknownMap[emittedMap + 4 + 12] = (byte) 255;
        invalidLayouts.add(unknownMap);

        for (byte[] invalid : invalidLayouts) {
            try {
                Api30HiddenApiMetadata.alignedTail(invalid, true);
                throw new AssertionError();
            } catch (Failure expected) {
                // Rejection is the expected outcome.
            }
        }
    }

    private static void checkMalformedRestore(byte[] raw, byte[] emitted) throws Exception {
        check(Arrays.equals(emitted, Api30HiddenApiMetadata.restore(raw, emitted)));
        byte[] corruptedInput = raw.clone();
        corruptedInput[100] ^= 1;

        try {
            Api30HiddenApiMetadata.restore(corruptedInput, emitted);
            throw new AssertionError();
        } catch (Failure expected) {
            // Rejection is the expected outcome.
        }

        java.util.List<byte[]> malformed = new java.util.ArrayList<>();
        malformed.add(new byte[0]);
        byte[] badMap = emitted.clone();
        java.nio.ByteBuffer.wrap(badMap)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN)
                .putInt(integer(badMap, 52), Integer.MAX_VALUE);
        malformed.add(badMap);
        byte[] badClassOffset = emitted.clone();
        java.nio.ByteBuffer.wrap(badClassOffset)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN)
                .putInt(100, Integer.MAX_VALUE);
        malformed.add(badClassOffset);
        byte[] overflow = emitted.clone();
        int definitions = integer(overflow, 100), data = 0;

        for (int index = 0; index < integer(overflow, 96) && data == 0; index++) {
            data = integer(overflow, definitions + index * 32 + 24);
        }

        check(data > 0);

        for (int index = 0; index < 4; index++) {
            overflow[data + index] = (byte) 128;
        }

        overflow[data + 4] = 16;
        malformed.add(overflow);
        byte[] overlong = emitted.clone();
        overlong[data] = (byte) 128;
        overlong[data + 1] = 0;
        malformed.add(overlong);

        for (byte[] invalid : malformed) {
            try {
                Api30HiddenApiMetadata.restore(raw, invalid);
                throw new AssertionError();
            } catch (Failure expected) {
                // Rejection is the expected outcome.
            }
        }
    }

    private static void checkRestrictions(byte[] raw, byte[] patched) {
        var rawRestrictions = restrictions(raw);
        var patchedRestrictions = restrictions(patched);

        for (var entry : rawRestrictions.entrySet()) {
            check(entry.getValue().equals(patchedRestrictions.get(entry.getKey())));
        }

        check(rawRestrictions.values().stream().anyMatch(value -> value == 2));
        check(rawRestrictions.values().stream().anyMatch(value -> value == 5));
        check(rawRestrictions.values().stream().anyMatch(value -> (value & 8) != 0));
        check(rawRestrictions.values().stream().anyMatch(value -> (value & 16) != 0));
    }

    private static void checkRejectedOwners(Path original, Path helper, Path output)
            throws Exception {
        for (String owner :
                new String[] {
                    "passion-11-russian-20260403-2.1.4.80", Api30ViewObservation.PROFILE
                }) {
            try {
                Api30ViewObservation.build(owner, 28, original, helper, output);
                throw new AssertionError();
            } catch (Failure expected) {
                // Rejection is the expected outcome.
            }
        }

        for (String owner :
                new String[] {
                    Api30ViewObservation.PROFILE, "passion-11-russian-20260403-2.1.4.80"
                }) {
            try {
                Api30ViewObservation.build(
                        owner, 30, output, helper, output.resolveSibling("twice.jar"));
                throw new AssertionError();
            } catch (Failure expected) {
                // Rejection is the expected outcome.
            }
        }
    }
}
