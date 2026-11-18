package com.voyemu.api;

import java.util.Map;

public final class EditorMetadataTest {
    private static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) {
        Map<String, Object> empty = EditorMetadata.fields(true, false, "Search", true, 0, 0);

        check(empty.get("hint").equals("Search") && Boolean.TRUE.equals(empty.get("showing_hint")));
        check(empty.get("selection_start").equals(0) && empty.get("selection_end").equals(0));
        String text = "Аa\uD83D\uDE00";
        Map<String, Object> entered =
                EditorMetadata.fields(true, false, null, false, 1, text.length());
        check(entered.get("hint").equals("") && Boolean.FALSE.equals(entered.get("showing_hint")));
        check(entered.get("selection_start").equals(1) && entered.get("selection_end").equals(4));
        check(entered.get("selection_unit").equals("utf16"));
        Map<String, Object> unknown = EditorMetadata.fields(true, false, "Hint", true, -1, -1);
        check(
                unknown.get("selection_status").equals("unknown")
                        && unknown.get("selection_start").equals(-1));

        for (boolean password : new boolean[] {false, true}) {
            Map<String, Object> unavailable =
                    EditorMetadata.fields(false, password, "private", true, 2, 9);
            check(unavailable.get("editor_status").equals(password ? "redacted" : "unsupported"));

            for (String key :
                    new String[] {"hint", "showing_hint", "selection_start", "selection_end"}) {
                check(!unavailable.containsKey(key));
            }
        }

        Map<String, Object> password = EditorMetadata.fields(true, true, "private", true, 1, 9);
        check(
                password.get("editor_status").equals("redacted")
                        && !password.containsKey("hint")
                        && !password.containsKey("selection_start"));
        System.out.println("observed editor metadata, UTF-16 and redaction tests passed");
    }
}
