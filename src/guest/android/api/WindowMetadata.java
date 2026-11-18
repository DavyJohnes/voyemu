package com.voyemu.api;

import com.voyemu.api.ViewServer.Window;

import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** WMS text metadata joins require the exact ViewServer WindowState identity hash. */
final class WindowMetadata {
    // Dropping trailing empty lines is intended.
    @SuppressWarnings("StringSplitter")
    static void assignDisplays(Map<String, Window> windows, String metadata) {
        Pattern heading =
                Pattern.compile("^(\\s*)Window #(\\d+) Window\\{([a-fA-F0-9]+) [^}]*\\}:.*$");
        Pattern display = Pattern.compile("\\bmDisplayId=(\\d+)\\b");

        assignFocus(windows, metadata);
        Window current = null;
        int currentIndent = -1;

        for (String line : metadata.split("\\r?\\n")) {
            Matcher h = heading.matcher(line);

            if (h.matches()) {
                current = windows.get(h.group(3).toLowerCase(java.util.Locale.ROOT));
                currentIndent = h.group(1).length();

                if (current != null) {
                    current.order = Integer.valueOf(h.group(2));
                }

                continue;
            }

            if (current == null) {
                continue;
            }

            int indent = 0;

            while (indent < line.length() && Character.isWhitespace(line.charAt(indent))) {
                ++indent;
            }

            // A sibling WMS section is outside this WindowState, even if it repeats its fields.
            if (indent < line.length() && indent <= currentIndent) {
                current = null;
                continue;
            }

            assignIdentity(current, line);
            assignState(current, line);
            Matcher d = display.matcher(line);

            if (d.find()) {
                int id = Integer.parseInt(d.group(1));

                if (current.display != null && current.display != id) {
                    throw new IllegalStateException("wm_display_conflict");
                }

                current.display = id;
            }
        }
    }

    private static void assignFocus(Map<String, Window> windows, String metadata) {
        Matcher focus =
                Pattern.compile("(?m)^\\s*mCurrentFocus=Window\\{([a-fA-F0-9]+) ")
                        .matcher(metadata);
        String focused = focus.find() ? focus.group(1).toLowerCase(java.util.Locale.ROOT) : null;

        if (focus.find()) {
            focused = null;
        }

        for (Window window : windows.values()) {
            window.currentFocus = focused == null ? null : focused.equals(window.hash);
        }
    }

    private static void assignIdentity(Window current, String line) {
        Matcher session =
                Pattern.compile("\\bmSession=Session\\{([a-fA-F0-9]+) (\\d+):(\\d+)\\}")
                        .matcher(line);

        if (session.find()) {
            current.sessionId = session.group(1).toLowerCase(java.util.Locale.ROOT);
            current.sessionPid = Integer.valueOf(session.group(2));
            current.sessionUid = Integer.valueOf(session.group(3));
        }

        Matcher client =
                Pattern.compile("\\bmClient=android\\.os\\.BinderProxy@([a-fA-F0-9]+)\\b")
                        .matcher(line);

        if (client.find()) {
            current.client = client.group(1).toLowerCase(java.util.Locale.ROOT);
        }

        Matcher token =
                Pattern.compile(
                                "^\\s+mToken=WindowToken\\{([a-fA-F0-9]+)"
                                        + " android\\.os\\.BinderProxy@([a-fA-F0-9]+)\\}")
                        .matcher(line);

        if (token.find() && token.group(2).equalsIgnoreCase(current.client)) {
            current.token = token.group(1).toLowerCase(java.util.Locale.ROOT);
        }

        Matcher obscured = Pattern.compile("\\bmObscured=(true|false)\\b").matcher(line);

        if (obscured.find()) {
            current.obscured = Boolean.valueOf(obscured.group(1));
        }

        Matcher f =
                Pattern.compile("\\bmFrame=\\[(-?\\d+),(-?\\d+)\\]\\[(-?\\d+),(-?\\d+)\\]")
                        .matcher(line);

        if (f.find()) {
            current.frame =
                    new int[] {
                        Integer.parseInt(f.group(1)),
                        Integer.parseInt(f.group(2)),
                        Integer.parseInt(f.group(3)),
                        Integer.parseInt(f.group(4))
                    };
        }
    }

    private static void assignState(Window current, String line) {
        Matcher flags = Pattern.compile("^\\s+fl=([A-Z0-9_ ]*)\\}?$").matcher(line);

        if (flags.matches()) {
            java.util.List<String> flagList =
                    java.util.Arrays.asList(flags.group(1).trim().split(" +"));
            current.touchable = !flagList.contains("NOT_TOUCHABLE");
            current.focusable = !flagList.contains("NOT_FOCUSABLE");
        }

        Matcher visible = Pattern.compile("^\\s+isVisible=(true|false)$").matcher(line);

        if (visible.matches()) {
            current.visible = Boolean.valueOf(visible.group(1));
        }

        Matcher surface = Pattern.compile("^\\s+mHasSurface=(true|false)\\b").matcher(line);

        if (surface.find()) {
            current.hasSurface = Boolean.valueOf(surface.group(1));
        }

        Matcher draw = Pattern.compile("\\bmDrawState=([A-Z_]+)\\b").matcher(line);

        if (draw.find()) {
            current.drawn = "HAS_DRAWN".equals(draw.group(1));
        }

        Matcher view = Pattern.compile("\\bmViewVisibility=0x([0-9a-fA-F]+)\\b").matcher(line);

        if (view.find()) {
            current.viewVisible = Integer.parseInt(view.group(1), 16) == 0;
        }

        Integer type = WindowTypeMetadata.parse(line);

        if (type != null) {
            current.type = type;
        }

        Matcher owner =
                Pattern.compile("\\bmOwnerUid=(\\d+).*?\\bpackage=([A-Za-z0-9_.]+)\\b")
                        .matcher(line);

        if (owner.find()) {
            current.ownerUid = Integer.parseInt(owner.group(1));
            current.ownerPackage = owner.group(2);
        }

        if (line.contains("mCompatFrame=")
                || line.contains("mHScale=")
                || line.contains("mVScale=")) {
            current.transformed = true;
        }
    }

    static String identities(Map<String, Window> windows) {
        StringBuilder result = new StringBuilder();

        for (Window window : windows.values()) {
            result.append(window.hash)
                    .append(':')
                    .append(window.display)
                    .append(':')
                    .append(window.title)
                    .append(':')
                    .append(java.util.Arrays.toString(window.frame))
                    .append(':')
                    .append(window.ownerUid)
                    .append(':')
                    .append(window.ownerPackage)
                    .append(':')
                    .append(window.type)
                    .append(':')
                    .append(window.transformed)
                    .append(':')
                    .append(window.touchable)
                    .append(':')
                    .append(window.visible)
                    .append(':')
                    .append(window.hasSurface)
                    .append(':')
                    .append(window.focusable)
                    .append(':')
                    .append(window.drawn)
                    .append(':')
                    .append(window.viewVisible)
                    .append(':')
                    .append(window.sessionPid)
                    .append(':')
                    .append(window.sessionUid)
                    .append(':')
                    .append(window.sessionId)
                    .append(':')
                    .append(window.token)
                    .append(':')
                    .append(window.client)
                    .append(':')
                    .append(window.order)
                    .append(':')
                    .append(window.currentFocus)
                    .append(':')
                    .append(window.obscured)
                    .append('\n');
        }

        return result.toString();
    }
}
