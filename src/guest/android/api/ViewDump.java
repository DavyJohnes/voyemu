package com.voyemu.api;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** API28 legacy DUMP grammar: lengths count decoded UTF16 units, not bytes. */
final class ViewDump {
    static final class Node {
        final String identity, parent;
        final Map<String, String> properties;

        Node(String identity, String parent, Map<String, String> properties) {
            this.identity = identity;
            this.parent = parent;
            this.properties = properties;
        }
    }

    static boolean terminal(String tail) {
        return tail.endsWith("\nDONE.\n") || tail.endsWith("\nDONE\n");
    }

    static String complete(byte[] bytes) throws java.nio.charset.CharacterCodingException {
        // Check only terminal framing here; parse still validates UTF16 lengths,
        // hierarchy identity, and the exact optional WindowManager envelope.
        int start = Math.max(0, bytes.length - 8);
        String tail =
                new String(
                        bytes,
                        start,
                        bytes.length - start,
                        java.nio.charset.StandardCharsets.US_ASCII);

        if (!terminal(tail)) {
            return null;
        }

        String dump =
                java.nio.charset.StandardCharsets.UTF_8
                        .newDecoder()
                        .onMalformedInput(java.nio.charset.CodingErrorAction.REPORT)
                        .onUnmappableCharacter(java.nio.charset.CodingErrorAction.REPORT)
                        .decode(java.nio.ByteBuffer.wrap(bytes))
                        .toString();

        try {
            parse(dump);

            return dump;
        } catch (IllegalArgumentException incomplete) {
            return null;
        }
    }

    static List<Node> parse(String dump) {
        if (dump.length() > 3 * 1024 * 1024) {
            throw new IllegalArgumentException("dump_limit");
        }

        List<Node> nodes = new ArrayList<>();
        List<String> parents = new ArrayList<>();
        java.util.HashSet<String> identities = new java.util.HashSet<>();
        int offset = 0;
        boolean done = false;

        while (offset < dump.length()) {
            if (dump.startsWith("DONE.", offset)) {
                offset += 5;
                done = true;
                break;
            }

            offset = node(dump, offset, nodes, parents, identities);
        }

        // ViewDebug terminates its hierarchy with DONE.; WindowManagerService may
        // append its own command acknowledgement DONE. Accept that exact envelope
        // only after a complete hierarchy, never a bare acknowledgement or tail data.
        String tail = dump.substring(offset).trim();

        if (!done || nodes.isEmpty() || (!tail.isEmpty() && !"DONE".equals(tail))) {
            throw new IllegalArgumentException("dump_incomplete");
        }

        return nodes;
    }

    /** Reads the node line at offset and returns the offset after its line breaks. */
    private static int node(
            String dump,
            int offset,
            List<Node> nodes,
            List<String> parents,
            java.util.Set<String> identities) {
        int depth = 0;

        while (offset < dump.length() && dump.charAt(offset) == ' ') {
            ++offset;
            ++depth;
        }

        if (depth > 80 || depth > parents.size() || nodes.size() >= 12000) {
            throw new IllegalArgumentException("dump_depth");
        }

        int end = identityEnd(dump, offset);
        String identity = dump.substring(offset, end);

        if (!identities.add(identity) || (depth == 0 && !nodes.isEmpty())) {
            throw new IllegalArgumentException("dump_duplicate_root_or_identity");
        }

        Map<String, String> props = new LinkedHashMap<>();
        offset = properties(dump, end + 1, props);
        nodes.add(new Node(identity, depth == 0 ? null : parents.get(depth - 1), props));

        while (parents.size() > depth) {
            parents.remove(parents.size() - 1);
        }

        parents.add(identity);

        while (offset < dump.length()
                && (dump.charAt(offset) == '\n' || dump.charAt(offset) == '\r')) {
            ++offset;
        }

        return offset;
    }

    private static int identityEnd(String dump, int offset) {
        int end = dump.indexOf(' ', offset);

        if (end < 0) {
            throw new IllegalArgumentException("dump_identity");
        }

        if (!dump.substring(offset, end).matches("[^\\s@]+@[a-fA-F0-9]+")) {
            throw new IllegalArgumentException("dump_identity");
        }

        return end;
    }

    private static int properties(String dump, int offset, Map<String, String> props) {
        while (offset < dump.length()
                && dump.charAt(offset) != '\n'
                && dump.charAt(offset) != '\r') {
            if (dump.charAt(offset) == ' ') {
                ++offset;
                continue;
            }

            offset = property(dump, offset, props);
        }

        return offset;
    }

    private static int property(String dump, int offset, Map<String, String> props) {
        int equals = dump.indexOf('=', offset),
                comma = equals < 0 ? -1 : dump.indexOf(',', equals + 1);

        if (equals < offset || comma < equals || comma - equals > 8) {
            throw new IllegalArgumentException("dump_property");
        }

        String key = dump.substring(offset, equals);

        if (!key.matches("[A-Za-z0-9_:()./]+")) {
            throw new IllegalArgumentException("dump_property");
        }

        int length;

        try {
            length = Integer.parseInt(dump.substring(equals + 1, comma));
        } catch (NumberFormatException error) {
            throw new IllegalArgumentException("dump_length");
        }

        if (length < 0 || length > 16384 || length > dump.length() - comma - 1) {
            throw new IllegalArgumentException("dump_length");
        }

        String value = dump.substring(comma + 1, comma + 1 + length);
        String previous = props.put(key, value);

        if (previous != null && !previous.equals(value)) {
            throw new IllegalArgumentException("dump_duplicate_property");
        }

        return separated(dump, comma + 1 + length);
    }

    private static int separated(String dump, int next) {
        if (next < dump.length()
                && dump.charAt(next) != ' '
                && dump.charAt(next) != '\r'
                && dump.charAt(next) != '\n') {
            throw new IllegalArgumentException("dump_length");
        }

        return next;
    }
}
