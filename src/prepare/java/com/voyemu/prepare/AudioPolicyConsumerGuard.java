package com.voyemu.prepare;

import org.jf.dexlib2.Opcode;
import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.ExceptionHandler;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.MethodImplementation;
import org.jf.dexlib2.iface.TryBlock;
import org.jf.dexlib2.iface.instruction.Instruction;
import org.jf.dexlib2.iface.instruction.ReferenceInstruction;
import org.jf.dexlib2.iface.instruction.formats.Instruction35c;
import org.jf.dexlib2.iface.reference.MethodReference;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.immutable.ImmutableExceptionHandler;
import org.jf.dexlib2.immutable.ImmutableMethod;
import org.jf.dexlib2.immutable.ImmutableMethodImplementation;
import org.jf.dexlib2.immutable.ImmutableTryBlock;
import org.jf.dexlib2.immutable.instruction.ImmutableInstruction35c;
import org.jf.dexlib2.immutable.reference.ImmutableMethodReference;

import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/** Private candidate only. Host semantic verification does not establish ART/Binder acceptance. */
final class AudioPolicyConsumerGuard {
    static final String ABI = "arm64-v8a";

    static final String OWNER = "Lcom/qinggan/audiopolicy/AudioPolicyManager;",
            SERVICE = "Lcom/qinggan/audiopolicy/IAudioPolicyService;";
    static final String REMOTE = "Landroid/os/RemoteException;",
            THROWABLE = "Ljava/lang/Throwable;",
            UOE = "Ljava/lang/UnsupportedOperationException;";

    record Pin(String name, List<String> params, String ret) {}

    static final List<Pin> PINS =
            List.of(
                    new Pin("isNaviMix", List.of(), "Z"),
                    new Pin("playSound", List.of("I"), "V"),
                    new Pin("playTone", List.of(), "V"),
                    new Pin("registerSoundChannelForPkg", List.of("Ljava/lang/String;", "I"), "V"),
                    new Pin("setBabyInCar", List.of("Z"), "I"),
                    new Pin("setEQModeByIndex", List.of("I", "[I"), "V"),
                    new Pin("setEcnr", List.of("Z"), "V"),
                    new Pin("setNaviMix", List.of("Z"), "V"),
                    new Pin("setSpeedCoefMode", List.of("I"), "I"),
                    new Pin("setVoiceMute", List.of("Z"), "V"),
                    new Pin("setXCallState", List.of("Z"), "V"),
                    new Pin("setMasterMute", List.of("Z", "I"), "V"),
                    new Pin("setMediaPlaying", List.of("Z", "Landroid/os/Bundle;"), "V"),
                    new Pin("setStreamMute", List.of("I", "Z"), "V"),
                    new Pin("setStreamVolume", List.of("I", "I", "I"), "V"),
                    new Pin("adjustStreamVolume", List.of("I", "I", "I", "I"), "V"),
                    new Pin("adjustVolume", List.of("I", "I"), "V"),
                    new Pin("unregisterSoundChannelForPkg", List.of("Ljava/lang/String;"), "V"));

    static void require(boolean ok) {
        Failure.require(
                ok,
                "AUDIO_CONSUMER_CONTRACT",
                "Exact private AudioPolicy consumer contract required",
                3);
    }

    static Pin pin(Method m) {
        if (!OWNER.equals(m.getDefiningClass())) {
            return null;
        }

        for (var p : PINS) {
            if (p.name.equals(m.getName())
                    && p.params.equals(m.getParameterTypes())
                    && p.ret.equals(m.getReturnType())) {
                return p;
            }
        }

        return null;
    }

    static Map<String, ClassDef> classes(byte[] dex) {
        var out = new TreeMap<String, ClassDef>();

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            out.put(c.getType(), c);
        }

        return out;
    }

    static Method withBody(Method m, MethodImplementation b) {
        return new ImmutableMethod(
                m.getDefiningClass(),
                m.getName(),
                m.getParameters(),
                m.getReturnType(),
                m.getAccessFlags(),
                m.getAnnotations(),
                m.getHiddenApiRestrictions(),
                b);
    }

    record Shape(int start, int length, int handler, int message, int binder) {}

    static Shape shape(Pin p) {
        return switch (p.name) {
            case "setStreamVolume" -> new Shape(5, 24, 30, 41, 17);
            case "setStreamMute", "adjustVolume" -> new Shape(5, 18, 24, 35, 11);
            case "adjustStreamVolume" -> new Shape(8, 18, 27, 38, 14);
            default -> {
                boolean value = !p.ret.equals("V");
                yield value ? new Shape(3, 20, 24, 35, 9) : new Shape(2, 18, 21, 32, 8);
            }
        };
    }

    static List<String> binderParams(Pin p) {
        return p.name.equals("setStreamVolume")
                ? List.of("I", "I", "I", "Ljava/lang/String;")
                : p.params;
    }

    static Method rewrite(Method m, boolean inverse) throws Exception {
        var p = pin(m);

        require(p != null && m.getImplementation() != null);

        var b = m.getImplementation();
        require(b.getTryBlocks().size() == 1);
        var t = b.getTryBlocks().get(0);
        boolean value = !p.ret.equals("V"), join = !p.name.equals("isNaviMix");
        Shape shape = shape(p);
        int start = shape.start,
                length = shape.length,
                handler = shape.handler,
                message = shape.message,
                binder = shape.binder;
        requireTryShape(t, shape, inverse, join);
        var instructions = new ArrayList<Instruction>();
        int pc = 0, changed = 0, calls = 0;

        for (var i : b.getInstructions()) {
            if (pc == binder) {
                requireBinderCall(i, p);
                calls++;
            }

            if (pc == message) {
                instructions.add(rewriteMessage(i, value, inverse));
                changed++;
            } else {
                instructions.add(i);
            }

            pc += i.getCodeUnits();
        }

        require(changed == 1 && calls == 1);
        var next = new ArrayList<ImmutableExceptionHandler>();
        next.add(new ImmutableExceptionHandler(REMOTE, handler));

        if (!inverse && join) {
            next.add(new ImmutableExceptionHandler(UOE, handler));
        }

        return withBody(
                m,
                new ImmutableMethodImplementation(
                        b.getRegisterCount(),
                        instructions,
                        List.of(new ImmutableTryBlock(start, length, next)),
                        b.getDebugItems()));
    }

    static void requireTryShape(
            TryBlock<? extends ExceptionHandler> t, Shape shape, boolean inverse, boolean join)
            throws Exception {
        require(t.getStartCodeAddress() == shape.start && t.getCodeUnitCount() == shape.length);
        var hs = t.getExceptionHandlers();
        require(hs.size() == (inverse && join ? 2 : 1));
        require(
                REMOTE.equals(hs.get(0).getExceptionType())
                        && hs.get(0).getHandlerCodeAddress() == shape.handler);

        if (inverse && join) {
            require(
                    UOE.equals(hs.get(1).getExceptionType())
                            && hs.get(1).getHandlerCodeAddress() == shape.handler);
        }
    }

    static void requireBinderCall(Instruction i, Pin p) throws Exception {
        require(i.getOpcode() == Opcode.INVOKE_INTERFACE && i instanceof ReferenceInstruction);
        var ref = ((ReferenceInstruction) i).getReference();
        require(ref instanceof MethodReference);
        var mr = (MethodReference) ref;
        require(
                SERVICE.equals(mr.getDefiningClass())
                        && p.name.equals(mr.getName())
                        && binderParams(p).equals(mr.getParameterTypes())
                        && p.ret.equals(mr.getReturnType()));
    }

    static Instruction rewriteMessage(Instruction i, boolean value, boolean inverse)
            throws Exception {
        require(i.getOpcode() == Opcode.INVOKE_VIRTUAL && i instanceof Instruction35c);
        var q = (Instruction35c) i;
        require(
                q.getRegisterCount() == 1
                        && q.getRegisterC() == (value ? 2 : 1)
                        && q.getRegisterD() == 0
                        && q.getRegisterE() == 0
                        && q.getRegisterF() == 0
                        && q.getRegisterG() == 0);
        require(q.getReference() instanceof MethodReference);
        var mr = (MethodReference) q.getReference();
        require(
                (inverse ? THROWABLE : REMOTE).equals(mr.getDefiningClass())
                        && mr.getName().equals("getMessage")
                        && mr.getParameterTypes().isEmpty()
                        && mr.getReturnType().equals("Ljava/lang/String;"));

        return new ImmutableInstruction35c(
                Opcode.INVOKE_VIRTUAL,
                q.getRegisterCount(),
                q.getRegisterC(),
                q.getRegisterD(),
                q.getRegisterE(),
                q.getRegisterF(),
                q.getRegisterG(),
                new ImmutableMethodReference(
                        inverse ? REMOTE : THROWABLE,
                        "getMessage",
                        List.of(),
                        "Ljava/lang/String;"));
    }

    static Map<String, ClassDef> transform(Map<String, ClassDef> input, boolean inverse)
            throws Exception {
        var out = new TreeMap<String, ClassDef>();
        int count = 0;

        for (var c : input.values()) {
            var direct = new ArrayList<Method>();
            var virtual = new ArrayList<Method>();
            int n = 0;

            for (var group : List.of(c.getDirectMethods(), c.getVirtualMethods())) {
                var dest = n++ == 0 ? direct : virtual;

                for (var m : group) {
                    if (pin(m) != null) {
                        dest.add(rewrite(m, inverse));
                        count++;
                    } else {
                        dest.add(m);
                    }
                }
            }

            out.put(
                    c.getType(),
                    new ImmutableClassDef(
                            c.getType(),
                            c.getAccessFlags(),
                            c.getSuperclass(),
                            c.getInterfaces(),
                            c.getSourceFile(),
                            c.getAnnotations(),
                            c.getStaticFields(),
                            c.getInstanceFields(),
                            direct,
                            virtual));
        }

        require(count == 18);

        return out;
    }

    static void validate(Map<String, Object> constants, int api, String abi, Path jar, byte[] dex)
            throws Exception {
        require(
                api == 30
                        && ABI.equals(abi)
                        && StepConstants.admits(constants, FilesSupport.hash(jar))
                        && dex != null);
        transform(classes(dex), false);
    }

    // dexlib2 writes hiddenapi rows in lexical class order but emits class_defs in
    // dependency order. ART requires aligned data and physically contiguous rows in
    // class_def order; preserve every original per-class flag byte, not merely offsets.
    static void correctWriterHiddenApiClassOffsets(byte[] dex, Set<String> expectedNames)
            throws Exception {
        var le = java.nio.ByteBuffer.wrap(dex).order(java.nio.ByteOrder.LITTLE_ENDIAN);
        int map = le.getInt(0x34), n = le.getInt(0x60);

        require(n == expectedNames.size());
        int hidden = -1, mapEntry = -1;

        for (int i = 0; i < le.getInt(map); i++) {
            int at = map + 4 + 12 * i;

            if ((le.getShort(at) & 0xffff) == 0xf000) {
                require(hidden == -1 && le.getInt(at + 4) == 1);
                hidden = le.getInt(at + 8);
                mapEntry = at;
            }
        }

        require(hidden >= 0 && hidden + 4 + 4 * n <= dex.length);
        int size = le.getInt(hidden), header = 4 + 4 * n;
        require(size >= header && hidden + size <= map);
        int aligned = (hidden + 3) & ~3;
        require(aligned + size <= map);

        for (int i = hidden + size; i < map; i++) {
            require(dex[i] == 0);
        }

        for (int i = 0; i < le.getInt(map); i++) {
            int at = map + 4 + 12 * i;
            int kind = le.getShort(at) & 0xffff;
            require(kind == 0xf000 || kind == 0x1000 || le.getInt(at + 8) < hidden);
        }

        var chunks = hiddenApiChunks(le, dex, expectedNames, hidden, size, header);
        byte[] rebuilt = rebuildHiddenApiRows(dex, expectedNames, chunks, size, header);
        Arrays.fill(dex, hidden, map, (byte) 0);
        System.arraycopy(rebuilt, 0, dex, aligned, size);
        le.putInt(mapEntry + 8, aligned);
        rewriteChecksums(dex, le);
    }

    // Same correction, growing the DEX by one word before the map list when the aligned rows do
    // not fit.
    static byte[] relocateWriterHiddenApiClassOffsets(byte[] dex, Set<String> expectedNames)
            throws Exception {
        var le = java.nio.ByteBuffer.wrap(dex).order(java.nio.ByteOrder.LITTLE_ENDIAN);
        int map = le.getInt(0x34), n = le.getInt(0x60);

        require(
                n == expectedNames.size()
                        && map >= 0
                        && (map & 3) == 0
                        && map + 4L <= dex.length
                        && le.getInt(0x2c) == 0
                        && le.getInt(0x30) == 0);
        require(le.getInt(map) >= 0 && map + 4L + 12L * le.getInt(map) == dex.length);
        int hidden = -1, mapEntry = -1, selfEntry = -1;

        for (int i = 0; i < le.getInt(map); i++) {
            int at = map + 4 + 12 * i, kind = le.getShort(at) & 0xffff;

            if (kind == 0xf000) {
                require(hidden == -1 && le.getInt(at + 4) == 1);
                hidden = le.getInt(at + 8);
                mapEntry = at;
            }

            if (kind == 0x1000) {
                require(selfEntry < 0 && le.getInt(at + 4) == 1 && le.getInt(at + 8) == map);
                selfEntry = at;
            }
        }

        require(selfEntry >= 0);
        require(hidden >= 0 && hidden + 4 + 4 * n <= dex.length);
        int size = le.getInt(hidden), header = 4 + 4 * n;
        require(size >= header && hidden + size <= map);
        int aligned = (hidden + 3) & ~3;

        for (int i = hidden + size; i < map; i++) {
            require(dex[i] == 0);
        }

        int growth = aligned + size > map ? 4 : 0;
        require(aligned + size <= map + growth);
        requireHiddenApiLastSection(le, dex, map, hidden);
        var chunks = hiddenApiChunks(le, dex, expectedNames, hidden, size, header);
        byte[] rebuilt = rebuildHiddenApiRows(dex, expectedNames, chunks, size, header);

        if (growth != 0) {
            dex = growBeforeMap(dex, map, growth);
            le = java.nio.ByteBuffer.wrap(dex).order(java.nio.ByteOrder.LITTLE_ENDIAN);
            mapEntry += growth;
        }

        Arrays.fill(dex, hidden, map + growth, (byte) 0);
        System.arraycopy(rebuilt, 0, dex, aligned, size);
        le.putInt(mapEntry + 8, aligned);
        rewriteChecksums(dex, le);

        return dex;
    }

    static void requireHiddenApiLastSection(
            java.nio.ByteBuffer le, byte[] dex, int map, int hidden) {
        require(
                map + 4L + 12L * le.getInt(map) == dex.length
                        && le.getInt(0x20) == dex.length
                        && (long) le.getInt(0x6c) + le.getInt(0x68) == dex.length);

        for (int i = 0; i < le.getInt(map); i++) {
            int at = map + 4 + 12 * i;
            int kind = le.getShort(at) & 0xffff;
            require(kind == 0xf000 || kind == 0x1000 || le.getInt(at + 8) < hidden);
        }
    }

    // Moves the map list by growth bytes and updates header and self references to it.
    static byte[] growBeforeMap(byte[] dex, int map, int growth) {
        int oldLength = dex.length;

        dex = Arrays.copyOf(dex, oldLength + growth);
        System.arraycopy(dex, map, dex, map + growth, oldLength - map);
        Arrays.fill(dex, map, map + growth, (byte) 0);
        var le = java.nio.ByteBuffer.wrap(dex).order(java.nio.ByteOrder.LITTLE_ENDIAN);
        le.putInt(0x20, dex.length);
        le.putInt(0x34, map + growth);
        le.putInt(0x68, le.getInt(0x68) + growth);
        int self = -1;

        for (int i = 0; i < le.getInt(map + growth); i++) {
            int at = map + growth + 4 + 12 * i;

            if ((le.getShort(at) & 0xffff) == 0x1000) {
                require(self < 0 && le.getInt(at + 4) == 1 && le.getInt(at + 8) == map);
                self = at;
            }
        }

        require(self >= 0);
        le.putInt(self + 8, map + growth);

        return dex;
    }

    // Splits writer rows, which follow lexical class order, into per-class flag bytes.
    static Map<String, byte[]> hiddenApiChunks(
            java.nio.ByteBuffer le,
            byte[] dex,
            Set<String> expectedNames,
            int hidden,
            int size,
            int header) {
        int n = expectedNames.size();
        var sorted = new ArrayList<>(expectedNames);

        Collections.sort(sorted);
        var chunks = new HashMap<String, byte[]>();
        int next = header;

        for (int i = 0; i < n; i++) {
            int offset = le.getInt(hidden + 4 + 4 * i);

            if (offset == 0) {
                continue;
            }

            require(offset == next && offset >= header && offset < size);
            int end = size;

            for (int j = i + 1; j < n; j++) {
                int following = le.getInt(hidden + 4 + 4 * j);

                if (following != 0) {
                    end = following;
                    break;
                }
            }

            require(end > offset && end <= size);
            chunks.put(sorted.get(i), Arrays.copyOfRange(dex, hidden + offset, hidden + end));
            next = end;
        }

        require(next == size);

        return chunks;
    }

    // Reassembles the per-class flag bytes contiguously in class_def order.
    static byte[] rebuildHiddenApiRows(
            byte[] dex, Set<String> expectedNames, Map<String, byte[]> chunks, int size, int header)
            throws Exception {
        int n = expectedNames.size();
        var actual = new DexBackedDexFile(Opcodes.forApi(30), dex);
        var order = new ArrayList<String>();
        var memberCounts = new HashMap<String, Integer>();

        for (var c : actual.getClasses()) {
            order.add(c.getType());
            int count = 0;

            for (var ignored : c.getFields()) {
                count++;
            }

            for (var ignored : c.getMethods()) {
                count++;
            }

            memberCounts.put(c.getType(), count);
        }

        require(order.size() == n && new HashSet<>(order).equals(expectedNames));
        byte[] rebuilt = new byte[size];
        var target = java.nio.ByteBuffer.wrap(rebuilt).order(java.nio.ByteOrder.LITTLE_ENDIAN);
        target.putInt(0, size);
        int cursor = header;

        for (int i = 0; i < n; i++) {
            byte[] chunk = chunks.get(order.get(i));

            if (chunk == null) {
                continue;
            }

            int consumed = 0;

            for (int member = 0; member < memberCounts.get(order.get(i)); member++) {
                int bytes = 0;

                while (true) {
                    require(consumed < chunk.length && bytes++ < 5);
                    int value = chunk[consumed++] & 255;

                    if ((value & 128) == 0) {
                        break;
                    }
                }
            }

            require(consumed == chunk.length);
            target.putInt(4 + 4 * i, cursor);
            System.arraycopy(chunk, 0, rebuilt, cursor, chunk.length);
            cursor += chunk.length;
        }

        require(cursor == size);

        return rebuilt;
    }

    static void rewriteChecksums(byte[] dex, java.nio.ByteBuffer le) throws Exception {
        var sha = java.security.MessageDigest.getInstance("SHA-1");

        sha.update(dex, 32, dex.length - 32);
        System.arraycopy(sha.digest(), 0, dex, 12, 20);
        var adler = new java.util.zip.Adler32();
        adler.update(dex, 12, dex.length - 12);
        le.putInt(8, (int) adler.getValue());
    }

    static void verifyHiddenApiLayout(byte[] dex) throws Exception {
        var le = java.nio.ByteBuffer.wrap(dex).order(java.nio.ByteOrder.LITTLE_ENDIAN);
        int map = le.getInt(0x34), n = le.getInt(0x60), hidden = -1;

        for (int i = 0; i < le.getInt(map); i++) {
            int at = map + 4 + 12 * i;

            if ((le.getShort(at) & 65535) == 0xf000) {
                require(hidden == -1 && le.getInt(at + 4) == 1);
                hidden = le.getInt(at + 8);
            }
        }

        require(hidden >= 0 && (hidden & 3) == 0 && hidden + 4 + 4 * n <= dex.length);
        int size = le.getInt(hidden), cursor = hidden + 4 + 4 * n;
        require(size >= 4 + 4 * n && hidden + size <= map);
        int i = 0;

        for (var c : new DexBackedDexFile(Opcodes.forApi(30), dex).getClasses()) {
            int offset = le.getInt(hidden + 4 + 4 * i++);

            if (offset == 0) {
                continue;
            }

            require(offset == cursor - hidden);
            int count = 0;

            for (var ignored : c.getFields()) {
                count++;
            }

            for (var ignored : c.getMethods()) {
                count++;
            }

            require(count > 0);

            for (int m = 0; m < count; m++) {
                int bytes = 0;

                while (true) {
                    require(cursor < hidden + size && bytes++ < 5);
                    int value = dex[cursor++] & 255;
                    require(bytes < 5 || (value & 240) == 0);

                    if ((value & 128) == 0) {
                        break;
                    }
                }
            }
        }

        require(i == n && cursor == hidden + size);
    }

    static String details(ClassDef c) throws Exception {
        var s = new StringBuilder();

        s.append("ANNS=").append(c.getAnnotations()).append(" IFACES=").append(c.getInterfaces());

        for (var m : c.getMethods()) {
            s.append(" METHOD=")
                    .append(m.toString())
                    .append(":")
                    .append(FrameworkDexMaterializer.semantic(m))
                    .append(" hidden=")
                    .append(m.getHiddenApiRestrictions());
        }

        return s.toString();
    }

    static void equal(Map<String, ClassDef> expected, Map<String, ClassDef> actual, String phase)
            throws Exception {
        require(expected.keySet().equals(actual.keySet()));

        for (var name : expected.keySet()) {
            if (!FrameworkDexMaterializer.semantic(expected.get(name))
                    .equals(FrameworkDexMaterializer.semantic(actual.get(name)))) {
                throw new Failure(
                        "AUDIO_CONSUMER_CONTRACT",
                        "Semantic drift in " + phase + " class " + name,
                        3);
            }
        }
    }

    /**
     * The step constant audio_consumer_relocate selects the hidden API correction that may grow the
     * DEX before its map list; otherwise rows are corrected in place.
     */
    static byte[] apply(Map<String, Object> constants, int api, String abi, Path jar, byte[] dex)
            throws Exception {
        validate(constants, api, abi, jar, dex);
        var before = classes(dex);
        var expected = transform(before, false);
        var output =
                FrameworkDexMaterializer.serialize(
                        new ImmutableDexFile(Opcodes.forApi(30), expected.values()));

        if (StepConstants.flag(constants, "audio_consumer_relocate")) {
            output = relocateWriterHiddenApiClassOffsets(output, expected.keySet());
            verifyHiddenApiLayout(output);
            var actual = classes(output);
            equal(expected, actual, "serialized");
            var inverse = transform(actual, true);
            equal(before, inverse, "inverse");

            return output;
        }

        correctWriterHiddenApiClassOffsets(output, expected.keySet());
        verifyHiddenApiLayout(output);
        var actual = classes(output);
        require(actual.keySet().equals(before.keySet()));

        for (var name : expected.keySet()) {
            if (!FrameworkDexMaterializer.semantic(expected.get(name))
                    .equals(FrameworkDexMaterializer.semantic(actual.get(name)))) {
                throw new IllegalStateException(
                        "serialized class "
                                + name
                                + " "
                                + details(expected.get(name))
                                + " / "
                                + details(actual.get(name)));
            }
        }

        var inverse = transform(actual, true);

        for (var name : before.keySet()) {
            if (!FrameworkDexMaterializer.semantic(before.get(name))
                    .equals(FrameworkDexMaterializer.semantic(inverse.get(name)))) {
                throw new IllegalStateException(
                        "inverse class "
                                + name
                                + " "
                                + FrameworkDexMaterializer.semantic(before.get(name))
                                + " / "
                                + FrameworkDexMaterializer.semantic(inverse.get(name)));
            }
        }

        return output;
    }
}
