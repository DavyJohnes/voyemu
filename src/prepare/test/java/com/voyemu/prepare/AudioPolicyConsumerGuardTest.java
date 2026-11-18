package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.formats.Instruction35c;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.ImmutableTryBlock;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction10x;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction35c;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.zip.ZipFile;

public final class AudioPolicyConsumerGuardTest {
    interface Checked {
        void run() throws Exception;
    }

    static int negatives;

    record MethodChanges(int changed, int unchanged) {}

    static void reject(Checked work) throws Exception {
        try {
            work.run();
        } catch (Failure e) {
            if (!e.code.equals("AUDIO_CONSUMER_CONTRACT")) {
                throw e;
            }

            negatives++;

            return;
        }

        throw new AssertionError("negative accepted");
    }

    static Method target(Map<String, ClassDef> c, String name) {
        for (var m : c.get(AudioPolicyConsumerGuard.OWNER).getMethods()) {
            if (m.getName().equals(name) && AudioPolicyConsumerGuard.pin(m) != null) {
                return m;
            }
        }

        throw new AssertionError(name);
    }

    static Method mutate(Method m, int kind) {
        var b = m.getImplementation();
        var list = new ArrayList<Instruction>();
        int pc = 0;

        for (var i : b.getInstructions()) {
            Instruction n = i;

            if (kind == 0 && pc == 0) {
                n = new ImmutableInstruction10x(Opcode.NOP);
            }

            if (kind == 1 && pc == 8) {
                var q = (Instruction35c) i;
                n =
                        new ImmutableInstruction35c(
                                q.getOpcode(),
                                q.getRegisterCount(),
                                q.getRegisterC(),
                                q.getRegisterD(),
                                q.getRegisterE(),
                                q.getRegisterF(),
                                q.getRegisterG(),
                                new ImmutableMethodReference(
                                        "Lwrong/Service;", "playSound", List.of("I"), "V"));
            }

            if (kind == 2 && pc == 32) {
                var q = (Instruction35c) i;
                n =
                        new ImmutableInstruction35c(
                                q.getOpcode(),
                                q.getRegisterCount(),
                                2,
                                0,
                                0,
                                0,
                                0,
                                q.getReference());
            }

            list.add(n);
            pc += i.getCodeUnits();
        }

        var ts = new ArrayList<ImmutableTryBlock>();

        for (var t : b.getTryBlocks()) {
            ts.add(
                    new ImmutableTryBlock(
                            t.getStartCodeAddress() + (kind == 3 ? 1 : 0),
                            t.getCodeUnitCount(),
                            t.getExceptionHandlers()));
        }

        return AudioPolicyConsumerGuard.withBody(
                m,
                new ImmutableMethodImplementation(
                        b.getRegisterCount(), list, ts, b.getDebugItems()));
    }

    // Arguments: PROFILE QGAPI_JAR WRONG_JAR.
    public static void main(String[] arguments) throws Exception {
        String profile = arguments[0];
        String[] a = Arrays.copyOfRange(arguments, 1, arguments.length);
        boolean relocate = AudioPolicyConsumerGuard.VARIANTS.get(profile).relocate();
        Path jar = Path.of(a[0]);
        byte[] before;

        try (var z = new ZipFile(jar.toFile())) {
            before = z.getInputStream(z.getEntry("classes.dex")).readAllBytes();
        }

        var original = AudioPolicyConsumerGuard.classes(before);
        checkOriginalUncovered(original);
        var expected = AudioPolicyConsumerGuard.transform(original, false);
        byte[] raw =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), expected.values()));
        byte[] corrected;
        var le = java.nio.ByteBuffer.wrap(raw).order(java.nio.ByteOrder.LITTLE_ENDIAN);
        int map = le.getInt(0x34), hidden = -1, n = le.getInt(0x60);

        for (int i = 0; i < le.getInt(map); i++) {
            int at = map + 4 + 12 * i;

            if ((le.getShort(at) & 65535) == 0xf000) {
                hidden = le.getInt(at + 8);
            }
        }

        if (hidden < 0) {
            throw new AssertionError("hiddenapi section missing");
        }

        corrected = corrected(relocate, raw, expected);

        int tableStart = hidden + 4;
        int entry = -1;

        for (int i = 0; i < le.getInt(map); i++) {
            int at = map + 4 + 12 * i;

            if ((le.getShort(at) & 65535) == 0xf000) {
                entry = at;
            }
        }

        int correctedHidden =
                correctedHidden(relocate, raw, corrected, expected, map, hidden, entry);

        if ((correctedHidden & 3) != 0) {
            throw new AssertionError("unaligned hiddenapi");
        }

        var cl = java.nio.ByteBuffer.wrap(corrected).order(java.nio.ByteOrder.LITTLE_ENDIAN);
        var sha = java.security.MessageDigest.getInstance("SHA-1");
        sha.update(corrected, 32, corrected.length - 32);

        if (!Arrays.equals(sha.digest(), Arrays.copyOfRange(corrected, 12, 32))) {
            throw new AssertionError("sha1");
        }

        var adler = new java.util.zip.Adler32();
        adler.update(corrected, 12, corrected.length - 12);

        if (cl.getInt(8) != (int) adler.getValue()) {
            throw new AssertionError("adler32");
        }

        String causal = "Lcom/excelfore/hmiagent/IDiagListener$Default;";
        var order = new ArrayList<String>();

        for (var c :
                new org.jf.dexlib2.dexbacked.DexBackedDexFile(Opcodes.forApi(30), raw)
                        .getClasses()) {
            order.add(c.getType());
        }

        int ci = order.indexOf(causal);
        int lexical = new ArrayList<>(expected.keySet()).indexOf(causal);
        System.out.println(
                "SERIALIZER_CAUSE hiddenapiOffset="
                        + hidden
                        + " classDefIndex="
                        + ci
                        + " lexicalIndex="
                        + lexical
                        + " class="
                        + causal
                        + " tableByteOffset="
                        + (tableStart + 4 * ci)
                        + " originalWrongOffset="
                        + le.getInt(tableStart + 4 * ci)
                        + " correctedOffset="
                        + cl.getInt(correctedHidden + 4 + 4 * ci)
                        + "; per-class payload preserved/reordered; SHA1/Adler32 verified");
        byte[] after =
                AudioPolicyConsumerGuard.apply(
                        profile, 30, AudioPolicyConsumerGuard.ABI, jar, before);
        AudioPolicyConsumerGuard.verifyHiddenApiLayout(after);
        byte[] bad = raw.clone();
        reject(() -> AudioPolicyConsumerGuard.verifyHiddenApiLayout(bad));
        checkRejectedLayouts(after, n);
        var actual = AudioPolicyConsumerGuard.classes(after);
        MethodChanges changes = countChangedMethods(original, actual);
        int changed = changes.changed(), unchanged = changes.unchanged();
        checkGuardScope(actual, changed);
        checkRejectedInputs(profile, a, jar, before, after, original);
        System.out.println(
                "PASS original uncovered UOE RED; exact 17 UOE joins + 18 Throwable.getMessage"
                        + " owner substitutions GREEN; isNaviMix propagates UOE; changedMethods="
                        + changed
                        + " unchangedMethods="
                        + unchanged
                        + " classes="
                        + actual.size()
                        + "; full serialized expected/inverse semantics verified; negatives="
                        + negatives
                        + " (profile/API/ABI/null/reapply/wrongJar/body/callsite/register/try/missingOwner);"
                        + " no broad catch; outputDEXsha256="
                        + NativeCore.hash(after)
                        + "; ART/Parcel/Binder runtime NOT VERIFIED");
    }

    static void checkRejectedLayouts(byte[] after, int n) throws Exception {
        byte[] wrongOffset = after.clone();
        var wo = java.nio.ByteBuffer.wrap(wrongOffset).order(java.nio.ByteOrder.LITTLE_ENDIAN);
        int hm = wo.getInt(0x34), hh = -1;

        for (int x = 0; x < wo.getInt(hm); x++) {
            int at = hm + 4 + 12 * x;

            if ((wo.getShort(at) & 65535) == 0xf000) {
                hh = wo.getInt(at + 8);
            }
        }

        int chosen = hh;
        wo.putInt(chosen + 4 + 4 * 4, wo.getInt(chosen + 4 + 4 * 4) + 1);
        reject(() -> AudioPolicyConsumerGuard.verifyHiddenApiLayout(wrongOffset));
        byte[] wrongAlignment = after.clone();
        var wa = java.nio.ByteBuffer.wrap(wrongAlignment).order(java.nio.ByteOrder.LITTLE_ENDIAN);

        for (int x = 0; x < wa.getInt(hm); x++) {
            int at = hm + 4 + 12 * x;

            if ((wa.getShort(at) & 65535) == 0xf000) {
                wa.putInt(at + 8, chosen - 2);
            }
        }

        reject(() -> AudioPolicyConsumerGuard.verifyHiddenApiLayout(wrongAlignment));
        byte[] wrongSize = after.clone();
        java.nio.ByteBuffer.wrap(wrongSize)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN)
                .putInt(chosen, 4 + 4 * n);
        reject(() -> AudioPolicyConsumerGuard.verifyHiddenApiLayout(wrongSize));
    }

    static void checkOriginalUncovered(Map<String, ClassDef> original) {
        int originalJoins = 0;

        for (var p : AudioPolicyConsumerGuard.PINS) {
            for (var h :
                    target(original, p.name())
                            .getImplementation()
                            .getTryBlocks()
                            .get(0)
                            .getExceptionHandlers()) {
                if (AudioPolicyConsumerGuard.UOE.equals(h.getExceptionType())) {
                    originalJoins++;
                }
            }
        }

        if (originalJoins != 0) {
            throw new AssertionError("RED absent");
        }
    }

    static byte[] corrected(boolean relocate, byte[] raw, Map<String, ClassDef> expected)
            throws Exception {
        if (relocate) {
            return AudioPolicyConsumerGuard.relocateWriterHiddenApiClassOffsets(
                    raw.clone(), expected.keySet());
        }

        byte[] corrected = raw.clone();
        AudioPolicyConsumerGuard.correctWriterHiddenApiClassOffsets(corrected, expected.keySet());

        return corrected;
    }

    // Returns the corrected hidden API offset after checking every byte outside the correction.
    static int correctedHidden(
            boolean relocate,
            byte[] raw,
            byte[] corrected,
            Map<String, ClassDef> expected,
            int map,
            int hidden,
            int entry)
            throws Exception {
        var le = java.nio.ByteBuffer.wrap(raw).order(java.nio.ByteOrder.LITTLE_ENDIAN);

        if (!relocate) {
            checkUnrelatedBytes(raw, corrected, hidden, map, entry);

            return java.nio.ByteBuffer.wrap(corrected)
                    .order(java.nio.ByteOrder.LITTLE_ENDIAN)
                    .getInt(entry + 8);
        }

        int growth = corrected.length - raw.length;

        if (growth != 0 && growth != 4) {
            throw new AssertionError("unexpected DEX growth");
        }

        var correctedLe =
                java.nio.ByteBuffer.wrap(corrected).order(java.nio.ByteOrder.LITTLE_ENDIAN);
        checkMapRelocation(raw, corrected, le, correctedLe, map, hidden, growth);
        checkRelocationFixtures(raw, corrected, expected, le, hidden, map, growth);

        return correctedLe.getInt(entry + growth + 8);
    }

    static void checkUnrelatedBytes(byte[] raw, byte[] corrected, int hidden, int map, int entry) {
        for (int i = 0; i < raw.length; i++) {
            if (raw[i] != corrected[i]
                    && !((i >= 8 && i < 32)
                            || (i >= hidden && i < map)
                            || (i >= entry + 8 && i < entry + 12))) {
                throw new AssertionError("unrelated byte changed " + i);
            }
        }
    }

    static void checkMapRelocation(
            byte[] raw,
            byte[] corrected,
            java.nio.ByteBuffer le,
            java.nio.ByteBuffer correctedLe,
            int map,
            int hidden,
            int growth) {
        int newMap = correctedLe.getInt(0x34);

        if (newMap != map + growth
                || correctedLe.getInt(0x20) != corrected.length
                || correctedLe.getInt(0x68) != le.getInt(0x68) + growth) {
            throw new AssertionError("DEX header relocation");
        }

        for (int i = 0; i < hidden; i++) {
            if (raw[i] != corrected[i]
                    && !((i >= 8 && i < 36)
                            || (i >= 0x34 && i < 0x38)
                            || (i >= 0x68 && i < 0x6c))) {
                throw new AssertionError("unrelated byte changed " + i);
            }
        }

        if (le.getInt(map) != correctedLe.getInt(newMap)) {
            throw new AssertionError("map count changed");
        }

        checkMapEntries(le, correctedLe, map, newMap);
    }

    static void checkMapEntries(
            java.nio.ByteBuffer le, java.nio.ByteBuffer correctedLe, int map, int newMap) {
        for (int i = 0; i < le.getInt(map); i++) {
            int oldAt = map + 4 + 12 * i,
                    newAt = newMap + 4 + 12 * i,
                    kind = le.getShort(oldAt) & 65535;

            if (le.getShort(oldAt) != correctedLe.getShort(newAt)
                    || le.getShort(oldAt + 2) != correctedLe.getShort(newAt + 2)
                    || le.getInt(oldAt + 4) != correctedLe.getInt(newAt + 4)) {
                throw new AssertionError("map metadata drift");
            }

            if (kind != 0xf000
                    && kind != 0x1000
                    && le.getInt(oldAt + 8) != correctedLe.getInt(newAt + 8)) {
                throw new AssertionError("unrelated map offset changed");
            }

            if (kind == 0x1000 && correctedLe.getInt(newAt + 8) != newMap) {
                throw new AssertionError("map self offset");
            }
        }
    }

    static void checkRelocationFixtures(
            byte[] raw,
            byte[] corrected,
            Map<String, ClassDef> expected,
            java.nio.ByteBuffer le,
            int hiddenAt,
            int mapAt,
            int growth)
            throws Exception {
        int selfAt = -1, otherAt = -1;

        for (int i = 0; i < le.getInt(mapAt); i++) {
            int at = mapAt + 4 + 12 * i, kind = le.getShort(at) & 65535;

            if (kind == 0x1000) {
                selfAt = at;
            } else if (kind != 0xf000 && otherAt < 0) {
                otherAt = at;
            }
        }

        if (selfAt < 0 || otherAt < 0 || hiddenAt + le.getInt(hiddenAt) >= mapAt) {
            throw new AssertionError("relocation fixture missing expected map/gap");
        }

        final int selfEntry = selfAt, otherEntry = otherAt, gap = hiddenAt + le.getInt(hiddenAt);

        if (growth != 4) {
            throw new AssertionError("relocating preimage must exercise four-byte growth");
        }

        if (!Arrays.equals(
                corrected,
                AudioPolicyConsumerGuard.relocateWriterHiddenApiClassOffsets(
                        raw.clone(), expected.keySet()))) {
            throw new AssertionError("growth=4 must be deterministic");
        }

        byte[] padded = Arrays.copyOf(raw, raw.length + 4);
        System.arraycopy(raw, mapAt, padded, mapAt + 4, raw.length - mapAt);
        Arrays.fill(padded, mapAt, mapAt + 4, (byte) 0);
        var pb = java.nio.ByteBuffer.wrap(padded).order(java.nio.ByteOrder.LITTLE_ENDIAN);
        pb.putInt(0x20, padded.length);
        pb.putInt(0x34, mapAt + 4);
        pb.putInt(0x68, le.getInt(0x68) + 4);
        pb.putInt(selfEntry + 4 + 8, mapAt + 4);
        var paddedSha = java.security.MessageDigest.getInstance("SHA-1");
        paddedSha.update(padded, 32, padded.length - 32);
        System.arraycopy(paddedSha.digest(), 0, padded, 12, 20);
        var paddedAdler = new java.util.zip.Adler32();
        paddedAdler.update(padded, 12, padded.length - 12);
        pb.putInt(8, (int) paddedAdler.getValue());
        byte[] noGrowth =
                AudioPolicyConsumerGuard.relocateWriterHiddenApiClassOffsets(
                        padded.clone(), expected.keySet());

        if (noGrowth.length != padded.length
                || !Arrays.equals(
                        noGrowth,
                        AudioPolicyConsumerGuard.relocateWriterHiddenApiClassOffsets(
                                padded.clone(), expected.keySet()))) {
            throw new AssertionError("growth=0 must be deterministic");
        }

        AudioPolicyConsumerGuard.verifyHiddenApiLayout(noGrowth);
        byte[] badGap = raw.clone();
        badGap[gap] = 1;
        reject(() ->
                AudioPolicyConsumerGuard.relocateWriterHiddenApiClassOffsets(
                        badGap, expected.keySet()));
        byte[] badFile = raw.clone();
        java.nio.ByteBuffer.wrap(badFile)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN)
                .putInt(0x20, badFile.length - 1);
        reject(() ->
                AudioPolicyConsumerGuard.relocateWriterHiddenApiClassOffsets(
                        badFile, expected.keySet()));
        byte[] badData = raw.clone();
        java.nio.ByteBuffer.wrap(badData)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN)
                .putInt(0x68, le.getInt(0x68) - 1);
        reject(() ->
                AudioPolicyConsumerGuard.relocateWriterHiddenApiClassOffsets(
                        badData, expected.keySet()));
        byte[] badSelf = raw.clone();
        java.nio.ByteBuffer.wrap(badSelf)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN)
                .putInt(selfEntry + 8, mapAt - 4);
        reject(() ->
                AudioPolicyConsumerGuard.relocateWriterHiddenApiClassOffsets(
                        badSelf, expected.keySet()));
        byte[] badOther = raw.clone();
        java.nio.ByteBuffer.wrap(badOther)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN)
                .putInt(otherEntry + 8, hiddenAt + 1);
        reject(() ->
                AudioPolicyConsumerGuard.relocateWriterHiddenApiClassOffsets(
                        badOther, expected.keySet()));
        byte[] badLink = raw.clone();
        java.nio.ByteBuffer.wrap(badLink).order(java.nio.ByteOrder.LITTLE_ENDIAN).putInt(0x2c, 1);
        reject(() ->
                AudioPolicyConsumerGuard.relocateWriterHiddenApiClassOffsets(
                        badLink, expected.keySet()));
        byte[] afterMap = Arrays.copyOf(raw, raw.length + 4);
        reject(() ->
                AudioPolicyConsumerGuard.relocateWriterHiddenApiClassOffsets(
                        afterMap, expected.keySet()));
    }

    static MethodChanges countChangedMethods(
            Map<String, ClassDef> original, Map<String, ClassDef> actual) throws Exception {
        int changed = 0, unchanged = 0;

        for (var name : original.keySet()) {
            var bc = original.get(name);
            var ac = actual.get(name);
            var om = new TreeMap<String, Method>();

            for (var m : bc.getMethods()) {
                om.put(m.toString(), m);
            }

            for (var m : ac.getMethods()) {
                var prior = om.get(m.toString());

                if (prior == null) {
                    throw new AssertionError("method changed identity");
                }

                boolean diff =
                        !FrameworkDexMaterializer.semantic(prior)
                                .equals(FrameworkDexMaterializer.semantic(m));

                if (diff) {
                    if (AudioPolicyConsumerGuard.pin(m) == null) {
                        throw new AssertionError("unrelated method changed " + m);
                    }

                    changed++;
                } else {
                    unchanged++;
                }
            }
        }

        return new MethodChanges(changed, unchanged);
    }

    static void checkGuardScope(Map<String, ClassDef> actual, int changed) {
        int joins = 0, owners = 0;

        for (var p : AudioPolicyConsumerGuard.PINS) {
            var m = target(actual, p.name());
            var b = m.getImplementation();

            for (var t : b.getTryBlocks()) {
                for (var h : t.getExceptionHandlers()) {
                    if (AudioPolicyConsumerGuard.UOE.equals(h.getExceptionType())) {
                        if (p.name().equals("isNaviMix")) {
                            throw new AssertionError("true fallback joined");
                        }

                        joins++;
                    }

                    if (Set.of(
                                    "Ljava/lang/Error;",
                                    "Ljava/lang/Throwable;",
                                    "Ljava/lang/Exception;",
                                    "Ljava/lang/RuntimeException;")
                            .contains(h.getExceptionType())) {
                        throw new AssertionError("broad catch");
                    }
                }
            }

            for (var i : b.getInstructions()) {
                if (i instanceof org.jf.dexlib2.iface.instruction.ReferenceInstruction r
                        && r.getReference()
                                instanceof org.jf.dexlib2.iface.reference.MethodReference mr
                        && mr.getName().equals("getMessage")
                        && mr.getDefiningClass().equals(AudioPolicyConsumerGuard.THROWABLE)) {
                    owners++;
                }
            }
        }

        if (joins != 17 || owners != 18 || changed != 18) {
            throw new AssertionError("scope " + joins + "/" + owners + "/" + changed);
        }
    }

    static void checkRejectedInputs(
            String profile,
            String[] a,
            Path jar,
            byte[] before,
            byte[] after,
            Map<String, ClassDef> original)
            throws Exception {
        reject(() ->
                AudioPolicyConsumerGuard.validate(
                        "other", 30, AudioPolicyConsumerGuard.ABI, jar, before));
        reject(() ->
                AudioPolicyConsumerGuard.validate(
                        profile, 34, AudioPolicyConsumerGuard.ABI, jar, before));
        reject(() -> AudioPolicyConsumerGuard.validate(profile, 30, "x86_64", jar, before));
        reject(() ->
                AudioPolicyConsumerGuard.validate(
                        profile, 30, AudioPolicyConsumerGuard.ABI, jar, null));
        reject(() ->
                AudioPolicyConsumerGuard.validate(
                        profile, 30, AudioPolicyConsumerGuard.ABI, jar, after));
        reject(() ->
                AudioPolicyConsumerGuard.validate(
                        profile, 30, AudioPolicyConsumerGuard.ABI, Path.of(a[1]), before));

        for (int k = 0; k < 4; k++) {
            int mode = k;
            reject(() ->
                    AudioPolicyConsumerGuard.rewrite(
                            mutate(target(original, "playSound"), mode), false));
        }

        for (String extra :
                List.of(
                        "setMasterMute",
                        "setMediaPlaying",
                        "setStreamMute",
                        "setStreamVolume",
                        "adjustStreamVolume",
                        "adjustVolume")) {
            reject(() ->
                    AudioPolicyConsumerGuard.rewrite(mutate(target(original, extra), 0), false));
        }

        var semanticDrift = new TreeMap<>(original);
        String owner = AudioPolicyConsumerGuard.OWNER;
        String replacement =
                original.keySet().stream().filter(k -> !k.equals(owner)).findFirst().orElseThrow();
        semanticDrift.put(owner, original.get(replacement));
        reject(() -> AudioPolicyConsumerGuard.equal(original, semanticDrift, "inverse"));
        var missing = new TreeMap<>(original);
        missing.remove(AudioPolicyConsumerGuard.OWNER);
        reject(() -> AudioPolicyConsumerGuard.transform(missing, false));
    }
}
