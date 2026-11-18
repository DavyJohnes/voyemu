package com.voyemu.prepare;

import java.math.BigDecimal;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/** JSON values only; no reflection, class loading or executable configuration. */
final class Json {
    private final String input;
    private int position;
    private int depth;

    private Json(String input) {
        this.input = input;
    }

    static Object parse(String input) {
        Json parser = new Json(input);
        Object result = parser.value();

        parser.space();
        parser.require(parser.position == input.length(), "Trailing JSON data");

        return result;
    }

    static Map<String, Object> object(Object value) {
        if (!(value instanceof Map<?, ?> map)) {
            throw new IllegalArgumentException("Expected JSON object");
        }

        Map<String, Object> result = new LinkedHashMap<>();

        for (var entry : map.entrySet()) {
            if (!(entry.getKey() instanceof String key)) {
                throw new IllegalArgumentException("Invalid object key");
            }

            result.put(key, entry.getValue());
        }

        return result;
    }

    static Map<String, Object> obj(Map<String, Object> map, String key) {
        return object(map.get(key));
    }

    static String str(Map<String, Object> map, String key) {
        if (!(map.get(key) instanceof String value)) {
            throw new IllegalArgumentException("Expected string: " + key);
        }

        return value;
    }

    static String str(Map<String, Object> map, String key, String fallback) {
        return map.containsKey(key) ? str(map, key) : fallback;
    }

    static long integer(Map<String, Object> map, String key) {
        Object value = map.get(key);

        if (value instanceof Long number) {
            return number;
        }

        if (value instanceof BigDecimal number) {
            return number.longValueExact();
        }

        if (value instanceof Integer number) {
            return number.longValue();
        }

        throw new IllegalArgumentException("Expected integer: " + key);
    }

    static List<Object> list(Object value) {
        if (!(value instanceof List<?> list)) {
            throw new IllegalArgumentException("Expected array");
        }

        return new ArrayList<>(list);
    }

    static String write(Object value) {
        StringBuilder result = new StringBuilder();

        write(value, result, 0);

        return result.toString();
    }

    private static void write(Object value, StringBuilder out, int depth) {
        if (depth > 128) {
            throw new IllegalArgumentException("JSON nesting limit");
        }

        if (value == null || value instanceof Boolean) {
            out.append(value);
        } else if (value instanceof String text) {
            out.append('"');

            for (int i = 0; i < text.length(); i++) {
                char c = text.charAt(i);

                switch (c) {
                    case '"' -> out.append("\\\"");
                    case '\\' -> out.append("\\\\");
                    case '\n' -> out.append("\\n");
                    case '\r' -> out.append("\\r");
                    case '\t' -> out.append("\\t");
                    default -> {
                        if (c < 32) {
                            out.append(String.format("\\u%04x", (int) c));
                        } else {
                            out.append(c);
                        }
                    }
                }
            }

            out.append('"');
        } else if (value instanceof Number number) {
            if ((number instanceof Double d && !Double.isFinite(d))
                    || (number instanceof Float f && !Float.isFinite(f))) {
                throw new IllegalArgumentException("Non-finite JSON number");
            }

            out.append(number);
        } else if (value instanceof Map<?, ?> map) {
            out.append('{');
            boolean first = true;

            for (var entry : map.entrySet()) {
                if (!(entry.getKey() instanceof String)) {
                    throw new IllegalArgumentException("Invalid key");
                }

                if (!first) {
                    out.append(',');
                }

                first = false;
                write(entry.getKey(), out, depth + 1);
                out.append(':');
                write(entry.getValue(), out, depth + 1);
            }

            out.append('}');
        } else if (value instanceof Iterable<?> items) {
            out.append('[');
            boolean first = true;

            for (Object item : items) {
                if (!first) {
                    out.append(',');
                }

                first = false;
                write(item, out, depth + 1);
            }

            out.append(']');
        } else {
            throw new IllegalArgumentException(
                    "Unsupported JSON value: " + value.getClass().getName());
        }
    }

    private Object value() {
        space();
        require(position < input.length(), "Missing JSON value");
        require(++depth <= 128, "JSON nesting limit");

        try {
            return switch (input.charAt(position)) {
                case '{' -> object();
                case '[' -> array();
                case '"' -> string();
                case 't' -> literal("true", true);
                case 'f' -> literal("false", false);
                case 'n' -> literal("null", null);
                default -> number();
            };
        } finally {
            depth--;
        }
    }

    private Map<String, Object> object() {
        position++;
        Map<String, Object> result = new LinkedHashMap<>();
        space();

        if (take('}')) {
            return result;
        }

        do {
            space();
            require(
                    position < input.length() && input.charAt(position) == '"',
                    "Missing object key");
            String key = string();
            require(!result.containsKey(key), "Duplicate JSON key: " + key);
            space();
            require(take(':'), "Missing colon");
            result.put(key, value());
            space();

            if (take('}')) {
                return result;
            }

            require(take(','), "Missing object comma");
        } while (true);
    }

    private List<Object> array() {
        position++;
        List<Object> result = new ArrayList<>();
        space();

        if (take(']')) {
            return result;
        }

        do {
            result.add(value());
            space();

            if (take(']')) {
                return result;
            }

            require(take(','), "Missing array comma");
        } while (true);
    }

    private String string() {
        require(take('"'), "Missing string");
        StringBuilder result = new StringBuilder();

        while (position < input.length()) {
            char c = input.charAt(position++);

            if (c == '"') {
                return result.toString();
            }

            require(c >= 32, "Control character in JSON string");

            if (c != '\\') {
                result.append(c);
                continue;
            }

            require(position < input.length(), "Truncated escape");
            char escaped = input.charAt(position++);

            switch (escaped) {
                case '"', '\\', '/' -> result.append(escaped);
                case 'b' -> result.append('\b');
                case 'f' -> result.append('\f');
                case 'n' -> result.append('\n');
                case 'r' -> result.append('\r');
                case 't' -> result.append('\t');
                case 'u' -> {
                    require(position + 4 <= input.length(), "Truncated Unicode escape");
                    int code = 0;

                    for (int i = 0; i < 4; i++) {
                        int digit = Character.digit(input.charAt(position++), 16);
                        require(digit >= 0, "Invalid Unicode escape");
                        code = code * 16 + digit;
                    }

                    result.append((char) code);
                }
                default -> throw error("Invalid escape");
            }
        }

        throw error("Unterminated string");
    }

    private Object number() {
        int start = position;

        take('-');
        require(position < input.length(), "Missing number");

        if (take('0')) {
            require(position == input.length() || !digit(input.charAt(position)), "Leading zero");
        } else {
            require(
                    input.charAt(position) >= '1' && input.charAt(position) <= '9',
                    "Invalid number");

            while (position < input.length() && digit(input.charAt(position))) {
                position++;
            }
        }

        boolean decimal = false;

        if (take('.')) {
            decimal = true;
            int begin = position;

            while (position < input.length() && digit(input.charAt(position))) {
                position++;
            }

            require(position > begin, "Missing fraction");
        }

        if (take('e') || take('E')) {
            decimal = true;

            if (!take('+')) {
                take('-');
            }

            int begin = position;

            while (position < input.length() && digit(input.charAt(position))) {
                position++;
            }

            require(position > begin, "Missing exponent");
        }

        String text = input.substring(start, position);

        try {
            if (!decimal) {
                return Long.valueOf(text);
            }

            return new BigDecimal(text);
        } catch (NumberFormatException e) {
            throw error("Invalid number");
        }
    }

    private Object literal(String expected, Object value) {
        require(input.startsWith(expected, position), "Invalid literal");
        position += expected.length();

        return value;
    }

    private static boolean digit(char c) {
        return c >= '0' && c <= '9';
    }

    private boolean take(char c) {
        if (position < input.length() && input.charAt(position) == c) {
            position++;

            return true;
        }

        return false;
    }

    private void space() {
        while (position < input.length() && " \t\r\n".indexOf(input.charAt(position)) >= 0) {
            position++;
        }
    }

    private void require(boolean condition, String message) {
        if (!condition) {
            throw error(message);
        }
    }

    private IllegalArgumentException error(String message) {
        return new IllegalArgumentException(message + " at offset " + position);
    }
}
