package com.voyemu.api;

/** API28-independent bounds and state rules shared by tree and target lookup. */
public final class Geometry {
    private Geometry() {}

    public static int[] clip(int left, int top, int right, int bottom, int[] window) {
        return clip(left, top, right, bottom, window, 1920, 720);
    }

    public static int[] clip(
            int left, int top, int right, int bottom, int[] window, int width, int height) {
        if (width <= 0 || height <= 0) {
            throw new IllegalArgumentException("surface_size");
        }

        int l = Math.min(width, Math.max(0, Math.max(left, window[0])));
        int t = Math.min(height, Math.max(0, Math.max(top, window[1])));
        int r = Math.max(0, Math.min(width, Math.min(right, window[2])));
        int b = Math.max(0, Math.min(height, Math.min(bottom, window[3])));

        if (r <= l || b <= t) {
            return new int[] {l, t, l, t};
        }

        return new int[] {l, t, r, b};
    }

    public static boolean actionable(
            boolean visible, boolean enabled, boolean clickable, int[] clipped) {
        return visible
                && enabled
                && clickable
                && clipped[2] > clipped[0]
                && clipped[3] > clipped[1];
    }

    public static boolean resourceId(String value) {
        return value != null
                && value.length() <= 256
                && value.matches("[A-Za-z0-9_.]+:id/[A-Za-z0-9_]+");
    }

    public static boolean packageId(String value) {
        return value != null && value.length() <= 160 && value.matches("[A-Za-z0-9_.]+");
    }

    public static String text(CharSequence value, boolean password) {
        if (password) {
            return "";
        }

        String result = value == null ? "" : value.toString();

        if (result.length() > 16384) {
            throw new IllegalArgumentException("string_limit");
        }

        return result;
    }
}
