package com.voyemu.api;

import java.util.LinkedHashMap;
import java.util.Map;

/** Preserve observed UTF-16 offsets; never infer caret positions from focus or text. */
final class EditorMetadata {
    static Map<String, Object> fields(
            boolean editable,
            boolean password,
            CharSequence hint,
            boolean showingHint,
            int selectionStart,
            int selectionEnd) {
        Map<String, Object> result = new LinkedHashMap<>();
        String status = password ? "redacted" : editable ? "available" : "unsupported";
        boolean available = "available".equals(status);

        result.put("editor_status", status);

        if (available) {
            result.put("hint", Geometry.text(hint, false));
            result.put("showing_hint", showingHint);
            result.put("selection_start", selectionStart);
            result.put("selection_end", selectionEnd);
        }

        result.put(
                "selection_status",
                !available
                        ? status
                        : selectionStart >= 0 && selectionEnd >= 0 ? "available" : "unknown");
        result.put("selection_unit", available ? "utf16" : null);

        return result;
    }
}
