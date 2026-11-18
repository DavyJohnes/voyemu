package com.voyemu.api;

import java.util.LinkedHashMap;
import java.util.Map;

/** Closed field map for the pinned ViewDumpBridge v1; legacy text never passes this gate. */
final class BridgeMetadata {
    static boolean present(ViewDump.Node node) {
        return "1".equals(ViewProperties.value(node, "voyemu:bridge_version"));
    }

    static boolean privacyKnown(ViewDump.Node node) {
        String status = ViewProperties.value(node, "voyemu:text_status");
        String input = ViewProperties.value(node, "voyemu:input_type");

        return present(node)
                && ViewProperties.bool(node, "voyemu:sensitive") != null
                && ViewProperties.bool(node, "voyemu:editable") != null
                && ViewProperties.bool(node, "voyemu:password") != null
                && ("available".equals(status)
                        || "redacted".equals(status)
                        || "none".equals(status))
                && ("unknown".equals(input)
                        || ViewProperties.integer(node, "voyemu:input_type") != null);
    }

    static boolean textAllowed(ViewDump.Node node) {
        Integer input = ViewProperties.integer(node, "voyemu:input_type");

        if (input == null || input < 0) {
            return false;
        }

        int kind = input & 0xfff;

        return present(node)
                && Boolean.FALSE.equals(ViewProperties.bool(node, "voyemu:sensitive"))
                && Boolean.FALSE.equals(ViewProperties.bool(node, "voyemu:editable"))
                && Boolean.FALSE.equals(ViewProperties.bool(node, "voyemu:password"))
                && "available".equals(ViewProperties.value(node, "voyemu:text_status"))
                && kind != 0x81
                && kind != 0xe1
                && kind != 0x91
                && kind != 0x12;
    }

    static String text(ViewDump.Node node) {
        if (!textAllowed(node)) {
            return "";
        }

        String text = ViewProperties.value(node, "text:mText");

        // Bridge framing escapes LF/CR but does not escape literal backslashes: preserve wire text.
        return text == null ? "" : Geometry.text(text, false);
    }

    static String resourceId(ViewDump.Node node) {
        if (!present(node)) {
            return null;
        }

        String resource = ViewProperties.value(node, "voyemu:resource_name");

        return resource != null
                        && resource.length() <= 256
                        && resource.matches("[A-Za-z0-9_.]+:[A-Za-z0-9_]+/[A-Za-z0-9_.]+")
                ? resource
                : null;
    }

    private static void integers(ViewDump.Node node, Map<String, Object> output, String... names) {
        for (String name : names) {
            Integer value = ViewProperties.integer(node, "voyemu:" + name);
            output.put(name, value);
        }
    }

    private static void numbers(ViewDump.Node node, Map<String, Object> output, String... names) {
        for (String name : names) {
            output.put(name, ViewProperties.number(node, "voyemu:" + name));
        }
    }

    static Map<String, Object> layout(ViewDump.Node node) {
        Map<String, Object> output = new LinkedHashMap<>();

        if (!present(node)) {
            return output;
        }

        integers(
                node,
                output,
                "measured_width",
                "measured_height",
                "min_width",
                "min_height",
                "padding_left",
                "padding_top",
                "padding_right",
                "padding_bottom",
                "scroll_x",
                "scroll_y");
        String type = ViewProperties.value(node, "voyemu:layout_params_type");
        output.put(
                "params_type",
                "none".equals(type) || "basic".equals(type) || "margin".equals(type) ? type : null);

        if ("basic".equals(type) || "margin".equals(type)) {
            integers(node, output, "layout_width", "layout_height");
        }

        if ("margin".equals(type)) {
            integers(node, output, "margin_left", "margin_top", "margin_right", "margin_bottom");
        }

        return output;
    }

    static Map<String, Object> textStyle(ViewDump.Node node) {
        Map<String, Object> output = new LinkedHashMap<>();

        if (!present(node)) {
            return output;
        }

        numbers(node, output, "text_size_px", "text_scale_x", "letter_spacing");
        String argb = ViewProperties.value(node, "voyemu:current_text_color_argb");
        Long color = null;

        if (argb != null && argb.matches("[0-9]{1,10}")) {
            long parsed = Long.parseLong(argb);

            if (parsed <= 0xffffffffL) {
                color = parsed;
            }
        }

        output.put("current_text_color_argb", color);

        return output;
    }

    /** Bounded actual native Layout/Paint values; no inferred baseline, lines or font identity. */
    static Map<String, Object> textLayout(ViewDump.Node node) {
        Map<String, Object> out = new LinkedHashMap<>();

        if (!present(node)) {
            return out;
        }

        String status = ViewProperties.value(node, "voyemu:text_layout_status");

        if (!"available".equals(status)) {
            out.put(
                    "status",
                    "none".equals(status) || "redacted".equals(status) || "overflow".equals(status)
                            ? status
                            : "unavailable");

            return out;
        }

        if (!textAllowed(node)) {
            out.put("status", "redacted");

            return out;
        }

        try {
            if (!"1".equals(ViewProperties.value(node, "voyemu:text_layout_version"))) {
                throw new IllegalArgumentException();
            }

            putLayoutIntegers(node, out);
            int length = (Integer) out.get("text_length"), count = (Integer) out.get("line_count");

            if (length > 4096 || count < 1 || count > 32 || (Integer) out.get("font_style") > 3) {
                throw new IllegalArgumentException();
            }

            putLayoutStyle(node, out);
            java.util.List<Map<String, Object>> lines =
                    layoutLines(node, count, length, (Integer) out.get("height"));
            out.put("version", 1);
            out.put("status", "available");
            out.put("lines", lines);

            return out;
        } catch (IllegalArgumentException unavailable) {
            Map<String, Object> missing = new LinkedHashMap<>();
            missing.put("status", "unavailable");

            return missing;
        }
    }

    private static void putLayoutIntegers(ViewDump.Node node, Map<String, Object> out) {
        String[] names = {
            "text_length",
            "width",
            "height",
            "baseline",
            "gravity",
            "padding_left",
            "padding_right",
            "padding_top",
            "padding_bottom",
            "font_style",
            "paint_flags",
            "break_strategy",
            "hyphenation",
            "line_count"
        };

        for (String name : names) {
            Integer value = ViewProperties.integer(node, "voyemu:text_layout_" + name);

            if (value == null
                    || value < 0
                    || (!"gravity".equals(name) && !"paint_flags".equals(name) && value > 65536)) {
                throw new IllegalArgumentException();
            }

            out.put(name, value);
        }
    }

    private static void putLayoutStyle(ViewDump.Node node, Map<String, Object> out) {
        for (String name : new String[] {"spanned", "include_font_padding", "fake_bold"}) {
            Boolean value = ViewProperties.bool(node, "voyemu:text_layout_" + name);

            if (value == null) {
                throw new IllegalArgumentException();
            }

            out.put(name, value);
        }

        Double skew = ViewProperties.number(node, "voyemu:text_layout_skew_x");

        if (skew == null || Math.abs(skew) > 16) {
            throw new IllegalArgumentException();
        }

        out.put("skew_x", skew);
        String alignment = ViewProperties.value(node, "voyemu:text_layout_alignment");

        if (!java.util.Arrays.asList("ALIGN_NORMAL", "ALIGN_CENTER", "ALIGN_OPPOSITE")
                .contains(alignment)) {
            throw new IllegalArgumentException();
        }

        out.put("alignment", alignment);
    }

    private static java.util.List<Map<String, Object>> layoutLines(
            ViewDump.Node node, int count, int length, int height) {
        java.util.List<Map<String, Object>> lines = new java.util.ArrayList<>();
        int end = 0, bottom = 0;

        for (int i = 0; i < count; i++) {
            String wire = ViewProperties.value(node, "voyemu:text_layout_line_" + i);

            if (wire == null) {
                throw new IllegalArgumentException();
            }

            Map<String, Object> line = layoutLine(wire);
            int start = (Integer) line.get("start"),
                    next = (Integer) line.get("end"),
                    top = (Integer) line.get("top"),
                    baseline = (Integer) line.get("baseline"),
                    nextBottom = (Integer) line.get("bottom");

            if (start != end
                    || next < start
                    || next > length
                    || top < bottom
                    || baseline < top
                    || baseline > nextBottom
                    || nextBottom < top
                    || !java.util.Arrays.asList(-1, 1).contains(line.get("paragraph_direction"))
                    || (Integer) line.get("ellipsis_start") < 0
                    || (Integer) line.get("ellipsis_count") < 0
                    || (Integer) line.get("ellipsis_start") + (Integer) line.get("ellipsis_count")
                            > next - start) {
                throw new IllegalArgumentException();
            }

            end = next;
            bottom = nextBottom;
            lines.add(line);
        }

        if (end != length || bottom != height) {
            throw new IllegalArgumentException();
        }

        return lines;
    }

    private static Map<String, Object> layoutLine(String wire) {
        String[] lineNames = {
            "start",
            "end",
            "left",
            "right",
            "top",
            "baseline",
            "bottom",
            "ellipsis_start",
            "ellipsis_count",
            "paragraph_direction"
        };
        String[] parts = wire.split(",", -1);

        if (parts.length != lineNames.length) {
            throw new IllegalArgumentException();
        }

        Map<String, Object> line = new LinkedHashMap<>();

        for (int k = 0; k < parts.length; k++) {
            if (k == 2 || k == 3) {
                double v = Double.parseDouble(parts[k]);

                if (!Double.isFinite(v) || Math.abs(v) > 65536) {
                    throw new IllegalArgumentException();
                }

                line.put(lineNames[k], v);
            } else {
                if (!parts[k].matches("-?[0-9]{1,5}")) {
                    throw new IllegalArgumentException();
                }

                int v = Integer.parseInt(parts[k]);

                if (Math.abs(v) > 65536) {
                    throw new IllegalArgumentException();
                }

                line.put(lineNames[k], v);
            }
        }

        return line;
    }

    static Map<String, Object> image(ViewDump.Node node) {
        Map<String, Object> output = new LinkedHashMap<>();

        if (!present(node)) {
            return output;
        }

        String scale = ViewProperties.value(node, "voyemu:image_scale_type");
        output.put(
                "scale_type",
                scale != null
                                && scale.matches(
                                        "MATRIX|FIT_XY|FIT_START|FIT_CENTER|FIT_END|CENTER|CENTER_CROP|CENTER_INSIDE")
                        ? scale
                        : null);
        String matrix = ViewProperties.value(node, "voyemu:image_matrix");
        double[] values = null;

        if (matrix != null) {
            String[] parts = matrix.split(",", -1);

            if (parts.length == 9) {
                values = new double[9];

                for (int i = 0; i < 9; ++i) {
                    String part = parts[i];

                    if (!part.matches("-?[0-9]+(?:\\.[0-9]+)?(?:[Ee][+-]?[0-9]+)?")) {
                        values = null;
                        break;
                    }

                    values[i] = Double.parseDouble(part);

                    if (!Double.isFinite(values[i])) {
                        values = null;
                        break;
                    }
                }
            }
        }

        output.put("matrix", values);
        output.put("matrix_order", "android_matrix_0_to_8");
        String status = ViewProperties.value(node, "voyemu:drawable_status");

        if (status == null) {
            // Not an ImageView: the pinned bridge writes this status for every ImageView.
            return output;
        }

        if (drawable(node, output, "drawable")) {
            integers(
                    node,
                    output,
                    "drawable_intrinsic_width",
                    "drawable_intrinsic_height",
                    "drawable_bounds_left",
                    "drawable_bounds_top",
                    "drawable_bounds_right",
                    "drawable_bounds_bottom");
        }

        return output;
    }

    /** The live View.getBackground() of one bridge node, read in this observation. */
    static Map<String, Object> background(ViewDump.Node node) {
        Map<String, Object> output = new LinkedHashMap<>();

        if (present(node)) {
            drawable(node, output, "background");
        }

        return output;
    }

    /**
     * Puts drawable_status (available, none or unavailable), drawable_level and the drawable state
     * set from the wire fields voyemu:PREFIX_status, _level and _state; returns whether the
     * drawable is available. A level is never substituted: without one the drawable is unavailable.
     */
    private static boolean drawable(ViewDump.Node node, Map<String, Object> output, String prefix) {
        String status = ViewProperties.value(node, "voyemu:" + prefix + "_status");
        Integer level = ViewProperties.integer(node, "voyemu:" + prefix + "_level");

        if (!"available".equals(status) || level == null) {
            output.put("drawable_status", "none".equals(status) ? "none" : "unavailable");

            return false;
        }

        output.put("drawable_status", "available");
        output.put("drawable_level", level);
        int[] state = stateSet(ViewProperties.value(node, "voyemu:" + prefix + "_state"));
        output.put("drawable_state_status", state == null ? "unavailable" : "available");
        output.put("drawable_state", state);

        return true;
    }

    /** Ascending distinct nonzero attributes, as the bridge writes them, or null. */
    static int[] stateSet(String wire) {
        if (wire == null || !wire.matches("(?:[1-9][0-9]{0,9}(?:,[1-9][0-9]{0,9}){0,63})?")) {
            return null;
        }

        if (wire.isEmpty()) {
            return new int[0];
        }

        String[] parts = wire.split(",", -1);
        int[] state = new int[parts.length];

        for (int i = 0; i < parts.length; ++i) {
            long value = Long.parseLong(parts[i]);

            if (value > Integer.MAX_VALUE || (i > 0 && value <= state[i - 1])) {
                return null;
            }

            state[i] = (int) value;
        }

        return state;
    }
}
