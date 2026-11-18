package com.voyemu.passion.phone;

public final class PassionPhoneAdapter {
    public static final android.net.Uri URI = new android.net.Uri();

    public static void initialize(android.content.Context c) {
        throw new AssertionError("unexpected Android initialization");
    }

    public static void outgoing(android.content.Context c, String number) {
        throw new AssertionError("unexpected Android call");
    }

    public static boolean connected() {
        return false;
    }

    public static org.json.JSONObject control(
            android.content.Context c, String method, org.json.JSONObject payload) {
        throw new AssertionError("unexpected Android adapter; boundary test overrides it");
    }
}
