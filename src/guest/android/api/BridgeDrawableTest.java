package com.voyemu.api;

import java.util.Arrays;
import java.util.Map;

public final class BridgeDrawableTest {
    private static void require(boolean condition) {
        if (!condition) {
            throw new AssertionError();
        }
    }

    private static String entry(String key, String value) {
        return key + "=" + value.length() + "," + value + " ";
    }

    /** One bridge-v1 dump node as the pinned ViewDumpBridge frames it. */
    private static ViewDump.Node node(String... properties) {
        StringBuilder dump =
                new StringBuilder("android.widget.ImageView@7f01 ")
                        .append(entry("voyemu:bridge_version", "1"))
                        .append(entry("isSelected()", "false"))
                        .append(entry("voyemu:image_scale_type", "FIT_CENTER"));

        for (int i = 0; i < properties.length; i += 2) {
            dump.append(entry(properties[i], properties[i + 1]));
        }

        return ViewDump.parse(dump.append("\nDONE.\n").toString()).get(0);
    }

    private static ViewDump.Node level(String level) {
        return node(
                "voyemu:drawable_status", "available",
                "voyemu:drawable_intrinsic_width", "48",
                "voyemu:drawable_level", level,
                "voyemu:drawable_state", "");
    }

    private static void levels() {
        Map<String, Object> on = BridgeMetadata.image(level("1"));
        Map<String, Object> off = BridgeMetadata.image(level("0"));

        require("available".equals(on.get("drawable_status")));
        require(Integer.valueOf(1).equals(on.get("drawable_level")));
        require("available".equals(off.get("drawable_status")));
        require(Integer.valueOf(0).equals(off.get("drawable_level")));
        require(Integer.valueOf(48).equals(off.get("drawable_intrinsic_width")));
        // A LevelListDrawable switch changes only the level; selected stays false in both reads.
        on.remove("drawable_level");
        off.remove("drawable_level");
        require(on.keySet().equals(off.keySet()));

        for (String key : on.keySet()) {
            Object a = on.get(key), b = off.get(key);
            require(
                    a instanceof double[]
                            ? Arrays.equals((double[]) a, (double[]) b)
                            : a instanceof int[]
                                    ? Arrays.equals((int[]) a, (int[]) b)
                                    : String.valueOf(a).equals(String.valueOf(b)));
        }

        require(
                Integer.valueOf(10000)
                        .equals(BridgeMetadata.image(level("10000")).get("drawable_level")));
    }

    private static void absentAndUnavailable() {
        Map<String, Object> none = BridgeMetadata.image(node("voyemu:drawable_status", "none"));

        require("none".equals(none.get("drawable_status")) && !none.containsKey("drawable_level"));
        Map<String, Object> failed =
                BridgeMetadata.image(node("voyemu:drawable_status", "unavailable"));
        require(
                "unavailable".equals(failed.get("drawable_status"))
                        && !failed.containsKey("drawable_level")
                        && !failed.containsKey("drawable_intrinsic_width"));

        // An available drawable without a readable level is unavailable, never level 0.
        for (String bad : new String[] {null, "", "unknown", "1.5", "99999999999"}) {
            ViewDump.Node node =
                    bad == null
                            ? node("voyemu:drawable_status", "available")
                            : node(
                                    "voyemu:drawable_status",
                                    "available",
                                    "voyemu:drawable_level",
                                    bad);
            Map<String, Object> image = BridgeMetadata.image(node);
            require(
                    "unavailable".equals(image.get("drawable_status"))
                            && !image.containsKey("drawable_level"));
        }

        require(
                "unavailable"
                        .equals(
                                BridgeMetadata.image(node("voyemu:drawable_status", "broken"))
                                        .get("drawable_status")));
        ViewDump.Node text =
                ViewDump.parse(
                                "android.widget.TextView@1 "
                                        + entry("voyemu:bridge_version", "1")
                                        + "\nDONE.\n")
                        .get(0);
        require(!BridgeMetadata.image(text).containsKey("drawable_status"));
        ViewDump.Node legacy =
                ViewDump.parse(
                                "android.widget.ImageView@1 "
                                        + entry("mID", "id/icon")
                                        + "\nDONE.\n")
                        .get(0);
        require(
                BridgeMetadata.image(legacy).isEmpty()
                        && BridgeMetadata.background(legacy).isEmpty());
    }

    private static void stateSets() {
        ViewDump.Node pressed =
                node(
                        "voyemu:drawable_status", "available",
                        "voyemu:drawable_level", "0",
                        "voyemu:drawable_state", "16842910,16842913");
        Map<String, Object> image = BridgeMetadata.image(pressed);

        require("available".equals(image.get("drawable_state_status")));
        require(Arrays.equals((int[]) image.get("drawable_state"), new int[] {16842910, 16842913}));
        Map<String, Object> empty = BridgeMetadata.image(level("0"));
        require(((int[]) empty.get("drawable_state")).length == 0);

        // The bridge writes an ascending distinct set; any other shape is not reinterpreted.
        for (String bad :
                new String[] {
                    "unavailable", "16842913,16842910", "1,1", "0", "-1", "1,", "4294967296"
                }) {
            Map<String, Object> state =
                    BridgeMetadata.image(
                            node(
                                    "voyemu:drawable_status", "available",
                                    "voyemu:drawable_level", "1",
                                    "voyemu:drawable_state", bad));
            require(
                    "unavailable".equals(state.get("drawable_state_status"))
                            && state.get("drawable_state") == null
                            && Integer.valueOf(1).equals(state.get("drawable_level")));
        }

        Map<String, Object> missing =
                BridgeMetadata.image(
                        node("voyemu:drawable_status", "available", "voyemu:drawable_level", "1"));
        require("unavailable".equals(missing.get("drawable_state_status")));
    }

    private static void backgrounds() {
        Map<String, Object> on =
                BridgeMetadata.background(
                        node(
                                "voyemu:background_status", "available",
                                "voyemu:background_level", "1",
                                "voyemu:background_state", "16842919"));

        require(
                "available".equals(on.get("drawable_status"))
                        && Integer.valueOf(1).equals(on.get("drawable_level"))
                        && Arrays.equals((int[]) on.get("drawable_state"), new int[] {16842919}));
        Map<String, Object> off =
                BridgeMetadata.background(
                        node(
                                "voyemu:background_status", "available",
                                "voyemu:background_level", "0",
                                "voyemu:background_state", "16842919"));
        require(Integer.valueOf(0).equals(off.get("drawable_level")));
        Map<String, Object> none =
                BridgeMetadata.background(node("voyemu:background_status", "none"));
        require("none".equals(none.get("drawable_status")) && !none.containsKey("drawable_level"));
        // A bridge node without a background read is unavailable, not absent.
        Map<String, Object> unread = BridgeMetadata.background(node());
        require("unavailable".equals(unread.get("drawable_status")));
        Map<String, Object> failed =
                BridgeMetadata.background(node("voyemu:background_status", "unavailable"));
        require(
                "unavailable".equals(failed.get("drawable_status"))
                        && !failed.containsKey("drawable_level"));
        // Image and background of one node are read independently.
        ViewDump.Node both =
                node(
                        "voyemu:drawable_status", "available",
                        "voyemu:drawable_level", "1",
                        "voyemu:drawable_state", "",
                        "voyemu:background_status", "none");
        require(Integer.valueOf(1).equals(BridgeMetadata.image(both).get("drawable_level")));
        require("none".equals(BridgeMetadata.background(both).get("drawable_status")));
    }

    public static void main(String[] args) {
        levels();
        absentAndUnavailable();
        stateSets();
        backgrounds();
        System.out.println("Bridge drawable metadata tests passed");
    }
}
