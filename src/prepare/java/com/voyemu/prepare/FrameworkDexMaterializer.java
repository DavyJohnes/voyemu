package com.voyemu.prepare;

import org.jf.dexlib2.Opcodes;
import org.jf.dexlib2.dexbacked.DexBackedDexFile;
import org.jf.dexlib2.dexbacked.OatFile;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.immutable.ImmutableClassDef;
import org.jf.dexlib2.immutable.ImmutableDexFile;
import org.jf.dexlib2.writer.io.MemoryDataStore;
import org.jf.dexlib2.writer.pool.DexPool;

import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/**
 * Pinned Android9 framework leaf. No I/O, recipe registration, checksum self-blessing or image
 * mutation.
 */
final class FrameworkDexMaterializer {
    private FrameworkDexMaterializer() {}

    record Result(List<byte[]> dexFiles, List<String> semanticSha256) {}

    static Result materialize(byte[] oat, String oatPin, byte[] vdex, String vdexPin)
            throws Exception {
        byte[] unquickened = VdexQuickening.decode(oat, oatPin, vdex, vdexPin);
        // Read ORIGINAL encoded hiddenAPI access bits, not BootVdex.decode's tool-only flags.
        OatFile container = new OatFile(oat, () -> unquickened);
        List<byte[]> files = new ArrayList<>();
        List<String> fingerprints = new ArrayList<>();

        for (var source : container.getDexFiles()) {
            List<ClassDef> classes = new ArrayList<>();

            for (ClassDef type : source.getClasses()) {
                // of() uses the four original static/instance/direct/virtual iterables.
                // The aggregate constructor would reclassify encoded private methods.
                classes.add(ImmutableClassDef.of(type));
            }

            ImmutableDexFile immutable = new ImmutableDexFile(Opcodes.forApi(28), classes);
            String before = semantic(immutable.getClasses());
            VdexQuickening.require(before.equals(semantic(source.getClasses())));
            byte[] bytes = serialize(immutable);
            DexBackedDexFile reread = new DexBackedDexFile(Opcodes.forApi(28), bytes);
            String after = semantic(reread.getClasses());

            if (!before.equals(after)) {
                Map<String, ClassDef> original = new TreeMap<>();

                for (ClassDef type : immutable.getClasses()) {
                    original.put(type.getType(), type);
                }

                for (ClassDef type : reread.getClasses()) {
                    if (!semantic(type).equals(semantic(original.get(type.getType())))) {
                        throw new Failure(
                                "FRAMEWORK_SEMANTIC_CHANGED",
                                "Materialized definition changed: "
                                        + type.getType()
                                        + " "
                                        + difference(original.get(type.getType()), type),
                                5);
                    }
                }

                throw new Failure(
                        "FRAMEWORK_SEMANTIC_CHANGED",
                        "Materialized definition inventory changed",
                        5);
            }

            byte[] canonical = serialize(reread);
            DexBackedDexFile canonicalRead = new DexBackedDexFile(Opcodes.forApi(28), canonical);
            VdexQuickening.require(after.equals(semantic(canonicalRead.getClasses())));
            VdexQuickening.require(Arrays.equals(canonical, serialize(canonicalRead)));
            bytes = canonical;

            for (ClassDef type : reread.getClasses()) {
                for (var method : type.getMethods()) {
                    if (method.getImplementation() != null) {
                        for (var instruction : method.getImplementation().getInstructions()) {
                            VdexQuickening.require(!instruction.getOpcode().odexOnly());
                        }
                    }
                }
            }

            files.add(bytes);
            fingerprints.add(before);
        }

        return new Result(List.copyOf(files), List.copyOf(fingerprints));
    }

    static byte[] serialize(org.jf.dexlib2.iface.DexFile dex) throws Exception {
        MemoryDataStore store = new MemoryDataStore();

        try {
            DexPool pool = new DexPool(dex.getOpcodes());

            for (ClassDef type : dex.getClasses()) {
                pool.internClass(type);

                // Pinned dexlib2 2.5.2 ClassPool misses invoke-polymorphic reference2.
                for (var method : type.getMethods()) {
                    if (method.getImplementation() != null) {
                        for (var instruction : method.getImplementation().getInstructions()) {
                            if (instruction
                                    instanceof
                                    org.jf.dexlib2.iface.instruction.DualReferenceInstruction
                                            dualReferenceInstruction) {
                                var reference = dualReferenceInstruction.getReference2();
                                VdexQuickening.require(
                                        reference
                                                instanceof
                                                org.jf.dexlib2.iface.reference
                                                        .MethodProtoReference);
                                pool.protoSection.intern(
                                        (org.jf.dexlib2.iface.reference.MethodProtoReference)
                                                reference);
                            }
                        }
                    }
                }
            }

            pool.writeTo(store);

            return store.getData();
        } finally {
            store.close();
        }
    }

    // Compare every public dexlib2 iface property, including access bits, the four member
    // partitions, instruction operands/references, annotations, debug items and exception ranges.
    // Indexed DexBacked implementation details are deliberately excluded.
    static String semantic(Object value) throws Exception {
        MessageDigest digest = MessageDigest.getInstance("SHA-256");

        append(digest, value);

        return HexFormat.of().formatHex(digest.digest());
    }

    static void token(MessageDigest digest, String value) {
        byte[] bytes = new byte[value.length() * 2];

        for (int i = 0; i < value.length(); i++) {
            bytes[i * 2] = (byte) value.charAt(i);
            bytes[i * 2 + 1] = (byte) (value.charAt(i) >>> 8);
        }

        digest.update(java.nio.ByteBuffer.allocate(4).putInt(bytes.length).array());
        digest.update(bytes);
    }

    static void interfaces(Class<?> type, Map<String, Method> getters) {
        if (type == null) {
            return;
        }

        for (Class<?> iface : type.getInterfaces()) {
            if (iface.getName().startsWith("org.jf.dexlib2.iface.")) {
                for (Method method : iface.getMethods()) {
                    if (method.getParameterCount() == 0
                            && method.getName().startsWith("get")
                            && Modifier.isPublic(method.getModifiers())) {
                        if (!method.getName().equals("getMethods")
                                && !method.getName().equals("getFields")) {
                            getters.put(method.getName(), method);
                        }
                    }
                }
            }

            interfaces(iface, getters);
        }

        interfaces(type.getSuperclass(), getters);
    }

    static String difference(Object a, Object b) throws Exception {
        if (a == null || b == null) {
            return "null=" + (a == null) + "/" + (b == null);
        }

        if (a instanceof Iterable<?> && b instanceof Iterable<?>) {
            var x = ((Iterable<?>) a).iterator();
            var y = ((Iterable<?>) b).iterator();
            int i = 0;

            while (x.hasNext() && y.hasNext()) {
                Object left = x.next(), right = y.next();

                if (!semantic(left).equals(semantic(right))) {
                    return "[" + i + "]" + difference(left, right);
                }

                i++;
            }

            return "length";
        }

        Map<String, Method> getters = new TreeMap<>();
        interfaces(a.getClass(), getters);
        Map<String, Method> other = new TreeMap<>();
        interfaces(b.getClass(), other);

        for (var e : getters.entrySet()) {
            if (!other.containsKey(e.getKey())) {
                return "interfaces:" + a.getClass().getName() + "/" + b.getClass().getName();
            }

            Object x = e.getValue().invoke(a), y = other.get(e.getKey()).invoke(b);

            if (!semantic(x).equals(semantic(y))) {
                return e.getKey() + "/" + difference(x, y);
            }
        }

        return String.valueOf(a) + "/" + String.valueOf(b);
    }

    static boolean defaultValue(org.jf.dexlib2.iface.value.EncodedValue value) {
        if (value instanceof org.jf.dexlib2.iface.value.FloatEncodedValue floatEncodedValue) {
            return Float.floatToRawIntBits(floatEncodedValue.getValue()) == 0;
        }

        if (value instanceof org.jf.dexlib2.iface.value.DoubleEncodedValue doubleEncodedValue) {
            return Double.doubleToRawLongBits(doubleEncodedValue.getValue()) == 0;
        }

        return org.jf.dexlib2.util.EncodedValueUtils.isDefaultValue(value);
    }

    private static final Map<Class<?>, Map<String, Method>> GETTERS =
            new java.util.concurrent.ConcurrentHashMap<>();

    static void append(MessageDigest digest, Object value) throws Exception {
        if (value == null) {
            token(digest, "null");

            return;
        }

        if (value instanceof Float f) {
            token(digest, "float:" + Float.floatToRawIntBits(f));

            return;
        }

        if (value instanceof Double d) {
            token(digest, "double:" + Double.doubleToRawLongBits(d));

            return;
        }

        if (value instanceof String
                || value instanceof Character
                || value instanceof Number
                || value instanceof Boolean
                || value instanceof Enum<?>) {
            token(digest, value.toString());

            return;
        }

        if (value instanceof Iterable<?>) {
            appendItems(digest, (Iterable<?>) value);

            return;
        }

        appendProperties(digest, value);
    }

    static void appendItems(MessageDigest digest, Iterable<?> value) throws Exception {
        List<String> children = new ArrayList<>();

        for (Object item : value) {
            children.add(semantic(item));
        }

        if (value instanceof Set<?> && !(value instanceof java.util.SortedSet<?>)) {
            children.sort(String::compareTo);
        }

        token(digest, "items:" + children.size());

        for (String child : children) {
            token(digest, child);
        }
    }

    static void appendProperties(MessageDigest digest, Object value) throws Exception {
        Map<String, Method> getters =
                GETTERS.computeIfAbsent(
                        value.getClass(),
                        type -> {
                            Map<String, Method> found = new TreeMap<>();
                            interfaces(type, found);

                            return found;
                        });

        if (getters.isEmpty()) {
            throw new Failure(
                    "FRAMEWORK_SEMANTIC_UNSUPPORTED", "Unsupported DEX metadata interface", 5);
        }

        for (var entry : getters.entrySet()) {
            token(digest, entry.getKey());
            Object property = entry.getValue().invoke(value);

            if (entry.getKey().equals("getParameterTypes")) {
                List<String> types = new ArrayList<>();

                for (Object type : (Iterable<?>) property) {
                    types.add(type.toString());
                }

                property = types;
            }

            // DEX permits trailing static defaults to be implicit; DexPool canonicalizes them.
            if (entry.getKey().equals("getInitialValue")
                    && property instanceof org.jf.dexlib2.iface.value.EncodedValue
                    && defaultValue((org.jf.dexlib2.iface.value.EncodedValue) property)) {
                property = null;
            }

            append(digest, property);
        }
    }
}
