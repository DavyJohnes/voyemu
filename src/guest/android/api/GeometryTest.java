package com.voyemu.api;

public final class GeometryTest {
    private static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) {
        int[] window = new int[] {20, 30, 1900, 700};
        int[] partial = Geometry.clip(-50, 10, 100, 80, window);

        check(java.util.Arrays.equals(partial, new int[] {20, 30, 100, 80}));
        int[] outside = Geometry.clip(2000, 100, 2100, 150, window);
        check(!Geometry.actionable(true, true, true, outside));
        check(java.util.Arrays.equals(outside, new int[] {1920, 100, 1920, 100}));
        check(!Geometry.actionable(false, true, true, partial));
        check(!Geometry.actionable(true, false, true, partial));
        check(!Geometry.actionable(true, true, false, partial));
        check(Geometry.actionable(true, true, true, partial));
        check(Geometry.text("secret", true).isEmpty());
        check(Geometry.text(null, false).isEmpty());
        boolean rejected = false;

        try {
            Geometry.text(new String(new char[16385]), false);
        } catch (IllegalArgumentException expected) {
            rejected = true;
        }

        check(rejected);
        check(Geometry.resourceId("example.package:id/ap_switch"));
        check(!Geometry.resourceId("ap_switch"));
        check(!Geometry.resourceId("example:id/name/extra"));
        check(Geometry.packageId("example.package"));
        check(!Geometry.packageId("bad package"));
        System.out.println("bounds/actionability/password-redaction/limits passed");
    }
}
