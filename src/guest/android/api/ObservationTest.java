package com.voyemu.api;

public final class ObservationTest {
    private static void require(boolean condition) {
        if (!condition) {
            throw new AssertionError();
        }
    }

    private static void reject(String dump) {
        try {
            ViewDump.parse(dump);
            throw new AssertionError("accepted malformed dump");
        } catch (IllegalArgumentException expected) {
            // A malformed dump must be rejected.
        }
    }

    public static void main(String[] args) throws Exception {
        String text = "Привет, мир 😀";
        java.util.List<ViewDump.Node> nodes =
                ViewDump.parse(
                        "android.View@a text="
                                + text.length()
                                + ","
                                + text
                                + " mLeft=1,0 \n android.Child@b id=6,id/foo \nDONE.\n");

        require(nodes.size() == 2 && nodes.get(1).parent.equals("android.View@a"));
        require(nodes.get(0).properties.get("text").equals(text));
        require(ViewDump.parse("android.View@a text=1,x \nDONE.\nDONE\n").size() == 1);
        reject("DONE\n");
        reject("android.View@a text=1,x \nDONE\n");
        reject("android.View@a text=1,x \nDONE.\nDONE\nextra");
        reject("android.View@a text=1,x \nDONE.\nDONE\nDONE\n");
        reject("android.View@a text=1,😀 \nDONE.\n");
        reject("android.View@a text=1,x \n");
        reject("android.View@a text=1,x text=1,y \nDONE.\n");
        reject("android.View@a text=1,x \n  android.View@b text=1,x \nDONE.\n");
        java.util.Map<String, String> props = new java.util.LinkedHashMap<>();
        props.put("layout:mLeft", "0");
        props.put("layout:mTop", "0");
        props.put("layout:mRight", "100");
        props.put("layout:mBottom", "40");
        props.put("layout:getLocationOnScreen_x()", "20");
        props.put("layout:getLocationOnScreen_y()", "30");
        props.put("drawing:getScaleX()", "1.5");
        props.put("drawing:getScaleY()", "1");
        props.put("drawing:getRotation()", "0");
        props.put("drawing:getRotationX()", "0");
        props.put("drawing:getRotationY()", "0");
        props.put("mID", "id/consent");
        ViewDump.Node view = new ViewDump.Node("View@a", null, props);
        require(
                java.util.Arrays.equals(
                        ViewProperties.bounds(java.util.Collections.singletonList(view))
                                .get("View@a"),
                        new int[] {20, 30, 170, 70}));
        require("id/consent".equals(ViewProperties.resource(view)));
        props.put("drawing:getRotation()", "15");
        require(ViewProperties.bounds(java.util.Collections.singletonList(view)).isEmpty());
        props.remove("drawing:getRotation()");
        require(ViewProperties.bounds(java.util.Collections.singletonList(view)).isEmpty());
        require(ViewProperties.bool(view, "isEnabled()") == null);
        require(
                java.util.Arrays.equals(
                        Geometry.clip(0, 0, 2500, 1500, new int[] {0, 0, 3000, 2000}, 2560, 1440),
                        new int[] {0, 0, 2500, 1440}));
        java.util.Map<String, String> bridge = new java.util.LinkedHashMap<>();
        ViewDump.Node typed = new ViewDump.Node("Text@a", null, bridge);
        bridge.put("text:mText", "private-password");
        require(BridgeMetadata.text(typed).isEmpty());
        bridge.put("voyemu:bridge_version", "1");
        bridge.put("voyemu:sensitive", "false");
        bridge.put("voyemu:editable", "false");
        bridge.put("voyemu:password", "false");
        bridge.put("voyemu:input_type", "0");
        bridge.put("voyemu:text_status", "available");
        bridge.put("text:mText", text);
        bridge.put("voyemu:resource_name", "injected.pkg:id/consent");
        require(BridgeMetadata.privacyKnown(typed) && BridgeMetadata.text(typed).equals(text));
        StringBuilder bridgeDump = new StringBuilder("android.widget.TextView@a ");

        for (java.util.Map.Entry<String, String> property : bridge.entrySet()) {
            bridgeDump
                    .append(property.getKey())
                    .append('=')
                    .append(property.getValue().length())
                    .append(',')
                    .append(property.getValue())
                    .append(' ');
        }

        bridgeDump.append("\nDONE.\n");
        ViewDump.Node roundTrip = ViewDump.parse(bridgeDump.toString()).get(0);
        require(BridgeMetadata.text(roundTrip).equals(text));
        require(BridgeMetadata.resourceId(roundTrip).equals("injected.pkg:id/consent"));
        require("injected.pkg:id/consent".equals(BridgeMetadata.resourceId(typed)));
        bridge.put("voyemu:editable", "true");
        require(BridgeMetadata.text(typed).isEmpty());
        bridge.put("voyemu:editable", "false");
        bridge.put("voyemu:password", "true");
        require(BridgeMetadata.text(typed).isEmpty());
        bridge.put("voyemu:password", "false");
        bridge.put("voyemu:input_type", "129");
        require(BridgeMetadata.text(typed).isEmpty());
        bridge.put("voyemu:input_type", "0");
        bridge.put("voyemu:sensitive", "true");
        require(BridgeMetadata.text(typed).isEmpty());
        bridge.remove("voyemu:sensitive");
        require(!BridgeMetadata.privacyKnown(typed) && BridgeMetadata.text(typed).isEmpty());
        bridge.put("voyemu:bridge_version", "2");
        require(BridgeMetadata.resourceId(typed) == null);
        bridge.put("voyemu:bridge_version", "1");
        bridge.put("voyemu:layout_params_type", "margin");
        bridge.put("voyemu:layout_width", "-1");
        bridge.put("voyemu:layout_height", "-2");
        bridge.put("voyemu:margin_left", "-2147483648");
        bridge.put("voyemu:margin_right", "2147483648");
        require(BridgeMetadata.layout(typed).get("layout_width").equals(-1));
        require(BridgeMetadata.layout(typed).get("layout_height").equals(-2));
        require(BridgeMetadata.layout(typed).get("margin_left").equals(Integer.MIN_VALUE));
        require(BridgeMetadata.layout(typed).get("margin_right") == null);
        bridge.put("voyemu:current_text_color_argb", "4294967295");
        bridge.put("voyemu:text_size_px", "NaN");
        bridge.put("voyemu:letter_spacing", "0.25");
        require(BridgeMetadata.textStyle(typed).get("current_text_color_argb").equals(4294967295L));
        require(BridgeMetadata.textStyle(typed).get("text_size_px") == null);
        require(BridgeMetadata.textStyle(typed).get("letter_spacing").equals(0.25));
        bridge.put("voyemu:current_text_color_argb", "4294967296");
        require(BridgeMetadata.textStyle(typed).get("current_text_color_argb") == null);
        bridge.put("voyemu:image_scale_type", "CENTER_CROP");
        bridge.put("voyemu:image_matrix", "1,0,2,0,1,3,0,0,1");
        bridge.put("voyemu:drawable_status", "available");
        bridge.put("voyemu:drawable_intrinsic_width", "-1");
        bridge.put("voyemu:drawable_level", "0");
        require(((double[]) BridgeMetadata.image(typed).get("matrix"))[5] == 3);
        require(BridgeMetadata.image(typed).get("drawable_intrinsic_width").equals(-1));
        bridge.put("voyemu:image_matrix", "1,0,unknown,0,1,0,0,0,1");
        require(BridgeMetadata.image(typed).get("matrix") == null);
        bridge.put("voyemu:image_matrix", "1,0,0,0,1,0,0,1");
        require(BridgeMetadata.image(typed).get("matrix") == null);
        bridge.put("voyemu:drawable_status", "none");
        require(!BridgeMetadata.image(typed).containsKey("drawable_intrinsic_width"));
        EventJournal journal = new EventJournal(2, 1000);
        long armed = journal.watermark();
        journal.append(1, "during baseline");
        require(journal.read(armed, 0).size() == 1);
        require(journal.read(armed, 0).size() == 1); // Independent reader cursor.
        journal.append(2, "second");
        journal.append(3, "third");
        require(journal.gap(armed) && !journal.gap(1));
        final boolean[] woke = new boolean[1];
        Thread reader =
                new Thread(() -> {
                    try {
                        woke[0] = journal.read(3, 30000).isEmpty();
                    } catch (InterruptedException error) {
                        throw new AssertionError(error);
                    }
                });
        reader.start();
        journal.close();
        reader.join(1000);
        require(!reader.isAlive() && woke[0]);
        textLayoutCases();
        System.out.println("Observation parser/journal tests passed");
    }

    private static void textLayoutCases() {
        java.util.Map<String, String> p = new java.util.LinkedHashMap<>();

        p.put("voyemu:bridge_version", "1");
        p.put("voyemu:sensitive", "false");
        p.put("voyemu:editable", "false");
        p.put("voyemu:password", "false");
        p.put("voyemu:input_type", "0");
        p.put("voyemu:text_status", "available");
        p.put("voyemu:text_layout_status", "available");
        p.put("voyemu:text_layout_version", "1");
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
        String[] values = {
            "5", "100", "30", "24", "8388659", "2", "3", "1", "1", "0", "1281", "0", "0", "1"
        };

        for (int i = 0; i < names.length; i++) {
            p.put("voyemu:text_layout_" + names[i], values[i]);
        }

        p.put("voyemu:text_layout_spanned", "false");
        p.put("voyemu:text_layout_include_font_padding", "true");
        p.put("voyemu:text_layout_fake_bold", "false");
        p.put("voyemu:text_layout_skew_x", "0.0");
        p.put("voyemu:text_layout_alignment", "ALIGN_NORMAL");
        p.put("voyemu:text_layout_line_0", "0,5,0.0,70.0,0,22,30,0,0,1");
        ViewDump.Node n = new ViewDump.Node("TextView@a", null, p);
        java.util.Map<String, Object> actual = BridgeMetadata.textLayout(n);
        require(actual.get("status").equals("available") && actual.get("baseline").equals(24));
        require(((java.util.List<?>) actual.get("lines")).size() == 1);

        for (String bad :
                new String[] {
                    "0,6,0,70,0,22,30,0,0,1",
                    "0,5,NaN,70,0,22,30,0,0,1",
                    "0,5,0,70,0,31,30,0,0,1",
                    "0,5,0,70,0,22,30,0,0,0",
                    "0,5,0,70,0,22,30,4,2,1"
                }) {
            p.put("voyemu:text_layout_line_0", bad);
            require(BridgeMetadata.textLayout(n).get("status").equals("unavailable"));
        }

        p.put("voyemu:text_layout_line_0", "0,5,0.0,70.0,0,22,30,0,0,1");
        p.put("voyemu:text_layout_line_count", "33");
        require(BridgeMetadata.textLayout(n).get("status").equals("unavailable"));
        p.put("voyemu:text_layout_line_count", "1");
        p.put("voyemu:sensitive", "true");
        java.util.Map<String, Object> redacted = BridgeMetadata.textLayout(n);
        require(redacted.size() == 1 && redacted.get("status").equals("redacted"));
        p.put("voyemu:sensitive", "false");
        p.put("voyemu:text_layout_version", "2");
        require(BridgeMetadata.textLayout(n).get("status").equals("unavailable"));
    }
}
