package com.voyemu.prepare;

import org.jf.dexlib2.iface.Annotation;
import org.jf.dexlib2.iface.ClassDef;
import org.jf.dexlib2.iface.Field;
import org.jf.dexlib2.iface.Method;
import org.jf.dexlib2.iface.value.ArrayEncodedValue;
import org.jf.dexlib2.iface.value.BooleanEncodedValue;
import org.jf.dexlib2.iface.value.ByteEncodedValue;
import org.jf.dexlib2.iface.value.CharEncodedValue;
import org.jf.dexlib2.iface.value.DoubleEncodedValue;
import org.jf.dexlib2.iface.value.EncodedValue;
import org.jf.dexlib2.iface.value.FloatEncodedValue;
import org.jf.dexlib2.iface.value.IntEncodedValue;
import org.jf.dexlib2.iface.value.LongEncodedValue;
import org.jf.dexlib2.iface.value.ShortEncodedValue;
import org.jf.dexlib2.iface.value.StringEncodedValue;
import org.jf.dexlib2.iface.value.TypeEncodedValue;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

/** Compile-only JVM signatures from owner DEX metadata; never loads or packages them. */
final class Signatures {
    // Byte carrier only; record equality and hashing are never used.
    @SuppressWarnings("ArrayRecordComponent")
    record Attribute(int name, byte[] bytes) {}

    record Member(int flags, int name, int descriptor, List<Attribute> attributes) {}

    private static final class Pool {
        final List<byte[]> entries = new ArrayList<>();
        final Map<String, Integer> cache = new HashMap<>();

        int add(String key, int tag, byte[] body, boolean wide) throws Exception {
            Integer present = cache.get(key);

            if (present != null) {
                return present;
            }

            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            bytes.write(tag);
            bytes.write(body);
            int index = entries.size() + 1;
            entries.add(bytes.toByteArray());

            if (wide) {
                entries.add(null);
            }

            Failure.require(
                    entries.size() < 65535,
                    "DONOR_SIGNATURE_LIMIT",
                    "JVM constant pool exceeds limit",
                    5);
            cache.put(key, index);

            return index;
        }

        int utf(String text) throws Exception {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();

            new DataOutputStream(bytes).writeUTF(text);

            return add("utf:" + text, 1, bytes.toByteArray(), false);
        }

        int cls(String name) throws Exception {
            return add("class:" + name, 7, shorts(utf(name)), false);
        }

        int method(String owner, String name, String descriptor) throws Exception {
            int type =
                    add("type:" + name + descriptor, 12, shorts(utf(name), utf(descriptor)), false);

            return add("method:" + owner + name + descriptor, 10, shorts(cls(owner), type), false);
        }

        int constant(EncodedValue value) throws Exception {
            if (value instanceof StringEncodedValue item) {
                return add("string:" + item.getValue(), 8, shorts(utf(item.getValue())), false);
            }

            if (value instanceof LongEncodedValue item) {
                return add("long:" + item.getValue(), 5, number(item.getValue()), true);
            }

            if (value instanceof DoubleEncodedValue item) {
                return add(
                        "double:" + item.getValue(),
                        6,
                        number(Double.doubleToLongBits(item.getValue())),
                        true);
            }

            if (value instanceof FloatEncodedValue item) {
                return add(
                        "float:" + item.getValue(),
                        4,
                        integer(Float.floatToIntBits(item.getValue())),
                        false);
            }

            // Avoid conditional numeric promotion unboxing null for reference/default fields.
            Integer integer = null;

            if (value instanceof IntEncodedValue item) {
                integer = item.getValue();
            } else if (value instanceof ShortEncodedValue item) {
                integer = (int) item.getValue();
            } else if (value instanceof ByteEncodedValue item) {
                integer = (int) item.getValue();
            } else if (value instanceof CharEncodedValue item) {
                integer = (int) item.getValue();
            } else if (value instanceof BooleanEncodedValue item) {
                integer = item.getValue() ? 1 : 0;
            }

            return integer == null ? 0 : add("int:" + integer, 3, integer(integer), false);
        }
    }

    static Path jar(List<? extends ClassDef> selected, Path output) throws Exception {
        Map<String, byte[]> classes = new TreeMap<>();

        for (ClassDef type : selected) {
            classes.put(internal(type.getType()) + ".class", render(type, selected));
        }

        Failure.require(!classes.isEmpty(), "DONOR_CLASS_MISSING", "No donor signatures", 5);
        NativeCore.zip(output, classes, false);

        return output;
    }

    private static byte[] render(ClassDef type, List<? extends ClassDef> selected)
            throws Exception {
        Pool pool = new Pool();
        String name = internal(type.getType());
        String parent =
                type.getSuperclass() == null ? "java/lang/Object" : internal(type.getSuperclass());
        int thisClass = pool.cls(name), superClass = pool.cls(parent);
        List<Integer> interfaces = new ArrayList<>();

        for (String value : type.getInterfaces()) {
            interfaces.add(pool.cls(internal(value)));
        }

        List<Member> fields = new ArrayList<>(), methods = new ArrayList<>();
        boolean isInterface = (type.getAccessFlags() & 0x0200) != 0;

        for (Field field : type.getFields()) {
            fields.add(fieldMember(pool, field));
        }

        for (Method method : type.getMethods()) {
            if (method.getName().equals("<clinit>")) {
                continue;
            }

            methods.add(methodMember(pool, method, parent, isInterface));
        }

        List<Attribute> attributes = new ArrayList<>();
        signature(pool, type.getAnnotations(), attributes);
        innerClasses(pool, type, name, selected, attributes);
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(bytes);
        out.writeInt(0xcafebabe);
        out.writeShort(0);
        out.writeShort(52);
        out.writeShort(pool.entries.size() + 1);

        for (byte[] entry : pool.entries) {
            if (entry != null) {
                out.write(entry);
            }
        }

        out.writeShort((type.getAccessFlags() & 0x7631) | (isInterface ? 0 : 0x20));
        out.writeShort(thisClass);
        out.writeShort(superClass);
        out.writeShort(interfaces.size());

        for (int index : interfaces) {
            out.writeShort(index);
        }

        members(out, fields);
        members(out, methods);
        attributes(out, attributes);

        return bytes.toByteArray();
    }

    private static Member fieldMember(Pool pool, Field field) throws Exception {
        List<Attribute> attributes = new ArrayList<>();
        int constant = pool.constant(field.getInitialValue());

        if (constant != 0 && (field.getAccessFlags() & 0x18) == 0x18) {
            attributes.add(new Attribute(pool.utf("ConstantValue"), shorts(constant)));
        }

        signature(pool, field.getAnnotations(), attributes);

        return new Member(
                field.getAccessFlags() & 0x50df,
                pool.utf(field.getName()),
                pool.utf(field.getType()),
                attributes);
    }

    private static Member methodMember(Pool pool, Method method, String parent, boolean isInterface)
            throws Exception {
        String descriptor =
                "(" + String.join("", method.getParameterTypes()) + ")" + method.getReturnType();
        int flags = method.getAccessFlags() & 0x1dff;
        List<Attribute> attributes = new ArrayList<>();

        signature(pool, method.getAnnotations(), attributes);
        exceptions(pool, method.getAnnotations(), attributes);

        if (isInterface && !method.getName().equals("<init>") && (flags & 0x0008) == 0) {
            flags = (flags | 0x0401) & ~0x0100;
        } else if ((flags & 0x0400) == 0) {
            flags &= ~0x0100;
            attributes.add(stubCode(pool, method, parent, flags));
        }

        return new Member(flags, pool.utf(method.getName()), pool.utf(descriptor), attributes);
    }

    // Constructors call the superclass constructor; other methods return a default value.
    private static Attribute stubCode(Pool pool, Method method, String parent, int flags)
            throws Exception {
        byte[] code;

        if (method.getName().equals("<init>")) {
            int superMethod =
                    pool.method(
                            parent,
                            "<init>",
                            parent.equals("java/lang/Enum") ? "(Ljava/lang/String;I)V" : "()V");
            code =
                    parent.equals("java/lang/Enum")
                            ? new byte[] {
                                0x2a,
                                0x2b,
                                0x1c,
                                (byte) 0xb7,
                                (byte) (superMethod >>> 8),
                                (byte) superMethod,
                                (byte) 0xb1
                            }
                            : new byte[] {
                                0x2a,
                                (byte) 0xb7,
                                (byte) (superMethod >>> 8),
                                (byte) superMethod,
                                (byte) 0xb1
                            };
        } else {
            code = returns(method.getReturnType());
        }

        ByteArrayOutputStream body = new ByteArrayOutputStream();
        DataOutputStream data = new DataOutputStream(body);
        int locals = (flags & 0x0008) != 0 ? 0 : 1;

        for (CharSequence parameter : method.getParameterTypes()) {
            locals += parameter.toString().equals("J") || parameter.toString().equals("D") ? 2 : 1;
        }

        data.writeShort(3);
        data.writeShort(locals);
        data.writeInt(code.length);
        data.write(code);
        data.writeShort(0);
        data.writeShort(0);

        return new Attribute(pool.utf("Code"), body.toByteArray());
    }

    private static void innerClasses(
            Pool pool,
            ClassDef type,
            String name,
            List<? extends ClassDef> selected,
            List<Attribute> attributes)
            throws Exception {
        ByteArrayOutputStream innerBytes = new ByteArrayOutputStream();
        DataOutputStream inner = new DataOutputStream(innerBytes);
        List<? extends ClassDef> nested =
                selected.stream()
                        .filter(item ->
                                internal(item.getType()).contains("$")
                                        && (internal(item.getType()).startsWith(name + "$")
                                                || item.getType().equals(type.getType())))
                        .toList();

        if (!nested.isEmpty()) {
            inner.writeShort(nested.size());

            for (ClassDef item : nested) {
                String nestedName = internal(item.getType());
                int split = nestedName.lastIndexOf('$');
                String simple = nestedName.substring(split + 1);
                inner.writeShort(pool.cls(nestedName));
                inner.writeShort(pool.cls(nestedName.substring(0, split)));
                inner.writeShort(simple.matches("[0-9].*") ? 0 : pool.utf(simple));
                inner.writeShort(innerFlags(item));
            }

            attributes.add(new Attribute(pool.utf("InnerClasses"), innerBytes.toByteArray()));
        }
    }

    private static int innerFlags(ClassDef type) {
        for (Annotation annotation : type.getAnnotations()) {
            if (!annotation.getType().equals("Ldalvik/annotation/InnerClass;")) {
                continue;
            }

            for (var element : annotation.getElements()) {
                if (element.getName().equals("accessFlags")
                        && element.getValue() instanceof IntEncodedValue flags) {
                    return flags.getValue() & 0x761f;
                }
            }
        }

        return (type.getAccessFlags() & 0x761f) | 0x8;
    }

    private static void signature(
            Pool pool, Set<? extends Annotation> annotations, List<Attribute> attributes)
            throws Exception {
        for (Annotation annotation : annotations) {
            if (!annotation.getType().equals("Ldalvik/annotation/Signature;")) {
                continue;
            }

            for (var element : annotation.getElements()) {
                if (!element.getName().equals("value")
                        || !(element.getValue() instanceof ArrayEncodedValue array)) {
                    continue;
                }

                StringBuilder text = new StringBuilder();

                for (EncodedValue item : array.getValue()) {
                    Failure.require(
                            item instanceof StringEncodedValue,
                            "DONOR_SIGNATURE_INVALID",
                            "Invalid generic signature metadata",
                            5);
                    text.append(((StringEncodedValue) item).getValue());
                }

                attributes.add(
                        new Attribute(pool.utf("Signature"), shorts(pool.utf(text.toString()))));
            }
        }
    }

    private static void exceptions(
            Pool pool, Set<? extends Annotation> annotations, List<Attribute> attributes)
            throws Exception {
        for (Annotation annotation : annotations) {
            if (!annotation.getType().equals("Ldalvik/annotation/Throws;")) {
                continue;
            }

            for (var element : annotation.getElements()) {
                if (!element.getName().equals("value")
                        || !(element.getValue() instanceof ArrayEncodedValue array)) {
                    continue;
                }

                ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                DataOutputStream out = new DataOutputStream(bytes);
                out.writeShort(array.getValue().size());

                for (EncodedValue value : array.getValue()) {
                    Failure.require(
                            value instanceof TypeEncodedValue,
                            "DONOR_SIGNATURE_INVALID",
                            "Invalid throws metadata",
                            5);
                    out.writeShort(pool.cls(internal(((TypeEncodedValue) value).getValue())));
                }

                attributes.add(new Attribute(pool.utf("Exceptions"), bytes.toByteArray()));
            }
        }
    }

    private static byte[] returns(String type) {
        return switch (type) {
            case "V" -> new byte[] {(byte) 0xb1};
            case "J" -> new byte[] {0x09, (byte) 0xad};
            case "D" -> new byte[] {0x0e, (byte) 0xaf};
            case "F" -> new byte[] {0x0b, (byte) 0xae};
            default ->
                    type.startsWith("L") || type.startsWith("[")
                            ? new byte[] {0x01, (byte) 0xb0}
                            : new byte[] {0x03, (byte) 0xac};
        };
    }

    private static String internal(String type) {
        Failure.require(
                type.startsWith("L") && type.endsWith(";"),
                "DONOR_SIGNATURE_INVALID",
                "Expected reference type",
                5);

        return type.substring(1, type.length() - 1);
    }

    private static byte[] shorts(int... numbers) throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(bytes);

        for (int number : numbers) {
            out.writeShort(number);
        }

        return bytes.toByteArray();
    }

    private static byte[] integer(int number) throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();

        new DataOutputStream(bytes).writeInt(number);

        return bytes.toByteArray();
    }

    private static byte[] number(long number) throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();

        new DataOutputStream(bytes).writeLong(number);

        return bytes.toByteArray();
    }

    private static void members(DataOutputStream out, List<Member> members) throws Exception {
        out.writeShort(members.size());

        for (Member member : members) {
            out.writeShort(member.flags());
            out.writeShort(member.name());
            out.writeShort(member.descriptor());
            attributes(out, member.attributes());
        }
    }

    private static void attributes(DataOutputStream out, List<Attribute> attributes)
            throws Exception {
        out.writeShort(attributes.size());

        for (Attribute attribute : attributes) {
            out.writeShort(attribute.name());
            out.writeInt(attribute.bytes().length);
            out.write(attribute.bytes());
        }
    }
}
