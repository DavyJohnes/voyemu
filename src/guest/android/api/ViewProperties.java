package com.voyemu.api;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** Versioned AOSP android-9.0.0_r61 ExportedProperty allowlist, never encodeProperties names. */
final class ViewProperties {
    static String value(ViewDump.Node node, String key) {
        return node.properties.get(key);
    }

    static Integer integer(ViewDump.Node node, String key) {
        String value = value(node, key);

        if (value == null || !value.matches("-?[0-9]{1,10}")) {
            return null;
        }

        try {
            return Integer.valueOf(value);
        } catch (NumberFormatException invalid) {
            return null;
        }
    }

    static Double number(ViewDump.Node node, String key) {
        String value = value(node, key);

        if (value == null || !value.matches("-?[0-9]+(?:\\.[0-9]+)?(?:[Ee][+-]?[0-9]+)?")) {
            return null;
        }

        double result = Double.parseDouble(value);

        return Double.isFinite(result) ? result : null;
    }

    static Boolean bool(ViewDump.Node node, String key) {
        String value = value(node, key);

        return "true".equals(value) ? Boolean.TRUE : "false".equals(value) ? Boolean.FALSE : null;
    }

    static Map<String, int[]> bounds(List<ViewDump.Node> nodes) {
        Map<String, int[]> output = new HashMap<>();
        Map<String, double[]> scales = new HashMap<>();

        for (ViewDump.Node node : nodes) {
            int[] screen = screenBounds(node, scales);

            if (screen != null) {
                output.put(node.identity, screen);
            }
        }

        return output;
    }

    /** Returns the screen bounds of node and records its total scale, or null when unknown. */
    private static int[] screenBounds(ViewDump.Node node, Map<String, double[]> scales) {
        Integer left = integer(node, "layout:mLeft"),
                top = integer(node, "layout:mTop"),
                right = integer(node, "layout:mRight"),
                bottom = integer(node, "layout:mBottom"),
                x = integer(node, "layout:getLocationOnScreen_x()"),
                y = integer(node, "layout:getLocationOnScreen_y()");
        Double sx = number(node, "drawing:getScaleX()"), sy = number(node, "drawing:getScaleY()");
        double[] parent = node.parent == null ? new double[] {1, 1} : scales.get(node.parent);

        if (left == null
                || top == null
                || right == null
                || bottom == null
                || x == null
                || y == null
                || sx == null
                || sy == null
                || !unrotated(node)
                || parent == null
                || right < left
                || bottom < top) {
            return null;
        }

        double totalX = parent[0] * sx, totalY = parent[1] * sy;
        double endX = x + ((long) right - left) * totalX, endY = y + ((long) bottom - top) * totalY;

        if (!Double.isFinite(endX)
                || !Double.isFinite(endY)
                || Math.abs(endX) > Integer.MAX_VALUE - 1
                || Math.abs(endY) > Integer.MAX_VALUE - 1) {
            return null;
        }

        scales.put(node.identity, new double[] {totalX, totalY});

        return new int[] {
            (int) Math.floor(Math.min(x, endX)),
            (int) Math.floor(Math.min(y, endY)),
            (int) Math.ceil(Math.max(x, endX)),
            (int) Math.ceil(Math.max(y, endY))
        };
    }

    private static boolean unrotated(ViewDump.Node node) {
        Double rz = number(node, "drawing:getRotation()"),
                rx = number(node, "drawing:getRotationX()"),
                ry = number(node, "drawing:getRotationY()");

        return rz != null && rx != null && ry != null && rz == 0 && rx == 0 && ry == 0;
    }

    static String resource(ViewDump.Node node) {
        String id = value(node, "mID");

        return id != null && id.matches("[A-Za-z0-9_]+/[A-Za-z0-9_.]+") ? id : null;
    }
}
