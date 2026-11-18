package com.voyemu.prepare;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.zip.ZipFile;

/** Actual OEM manifest stays unchanged except its pool/map and two appended providers. */
public final class PhoneManifestProvidersTest {
    static int size(byte[] b, int p) {
        return ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN).getInt(p + 4);
    }

    static int type(byte[] b, int p) {
        return Short.toUnsignedInt(ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN).getShort(p));
    }

    static void need(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    static void reject(byte[] b) throws Exception {
        try {
            PhoneManifest.add(b);
            throw new AssertionError("accepted invalid XML");
        } catch (Failure expected) {
            need(expected.code.equals("PATCH_MANIFEST_INVALID"));
        }
    }

    static void providerDeclarations(byte[] xml) {
        java.util.List<String> strings = new java.util.ArrayList<>();

        for (int at = 8; at < xml.length; at += size(xml, at)) {
            if (type(xml, at) == 1) {
                ByteBuffer b = ByteBuffer.wrap(xml).order(ByteOrder.LITTLE_ENDIAN);
                int count = b.getInt(at + 8), start = b.getInt(at + 20);

                for (int i = 0; i < count; i++) {
                    int pos = at + start + b.getInt(at + 28 + i * 4),
                            length = Short.toUnsignedInt(b.getShort(pos));
                    strings.add(
                            new String(
                                    xml,
                                    pos + 2,
                                    length * 2,
                                    java.nio.charset.StandardCharsets.UTF_16LE));
                }
            }
        }

        int control = 0, peer = 0;
        ByteBuffer b = ByteBuffer.wrap(xml).order(ByteOrder.LITTLE_ENDIAN);

        for (int at = 8; at < xml.length; at += size(xml, at)) {
            if (type(xml, at) == 0x102 && "provider".equals(strings.get(b.getInt(at + 20)))) {
                java.util.Map<String, String> attrs = new java.util.HashMap<>();
                int count = Short.toUnsignedInt(b.getShort(at + 28));

                for (int i = 0; i < count; i++) {
                    int pos = at + 16 + Short.toUnsignedInt(b.getShort(at + 24)) + i * 20;
                    attrs.put(
                            strings.get(b.getInt(pos + 4)),
                            xml[pos + 15] == 3
                                    ? strings.get(b.getInt(pos + 16))
                                    : Integer.toString(b.getInt(pos + 16)));
                }

                if (PhoneManifest.PROVIDER.equals(attrs.get("name"))) {
                    control++;
                    need(
                            attrs.size() == 4
                                    && "android.permission.DUMP".equals(attrs.get("permission"))
                                    && PhoneManifest.AUTHORITY.equals(attrs.get("authorities"))
                                    && "-1".equals(attrs.get("exported")));
                }

                if (PhoneManifest.PEER_PROVIDER.equals(attrs.get("name"))) {
                    peer++;
                    need(
                            attrs.size() == 3
                                    && !attrs.containsKey("permission")
                                    && !attrs.containsKey("process")
                                    && PhoneManifest.PEER_AUTHORITY.equals(attrs.get("authorities"))
                                    && "-1".equals(attrs.get("exported")));
                }
            }
        }

        need(control == 1 && peer == 1);
    }

    public static void main(String[] args) throws Exception {
        byte[] original;

        try (ZipFile zip = new ZipFile(args[0])) {
            original = zip.getInputStream(zip.getEntry("AndroidManifest.xml")).readAllBytes();
        }

        byte[] saved = original.clone(), output = PhoneManifest.add(original);
        need(Arrays.equals(saved, original));
        need(size(output, 0) == output.length);
        int cursor = 8, oldCount = 0, newCount = 0;

        for (int at = 8; at < original.length; at += size(original, at)) {
            if (type(original, at) == 1 || type(original, at) == 0x180) {
                continue;
            }

            byte[] chunk = Arrays.copyOfRange(original, at, at + size(original, at));
            boolean found = false;

            while (cursor < output.length) {
                int next = cursor + size(output, cursor);

                if (Arrays.equals(chunk, Arrays.copyOfRange(output, cursor, next))) {
                    found = true;
                    cursor = next;
                    break;
                }

                cursor = next;
            }

            need(found);
            oldCount++;
        }

        for (int at = 8; at < output.length; at += size(output, at)) {
            if (type(output, at) != 1 && type(output, at) != 0x180) {
                newCount++;
            }
        }

        need(newCount == oldCount + 4);
        providerDeclarations(output);
        reject(output);
        reject(new byte[0]);
        reject(Arrays.copyOf(original, original.length - 1));
        byte[] changed = original.clone(),
                name =
                        "com.qinggan.bluetoothphone.AppMain"
                                .getBytes(java.nio.charset.StandardCharsets.UTF_16LE);

        for (int at = 0; at <= changed.length - name.length; at++) {
            if (Arrays.equals(name, Arrays.copyOfRange(changed, at, at + name.length))) {
                changed[at] = 'x';
                break;
            }
        }

        reject(changed);
        Files.write(Path.of(args[1]), output);
        System.out.println(
                "PASS exact XML element preservation, separate peer/control permissions, two"
                        + " appended providers, duplicate/truncation/owner negatives");
    }
}
