package com.voyemu.api;

/** WindowManager.LayoutParams numeric type or exact stock symbols. */
final class WindowTypeMetadata {
    private static final java.util.regex.Pattern TYPE =
            java.util.regex.Pattern.compile(
                    "\\bty=(\\d{1,4}|WALLPAPER|NAVIGATION_BAR_PANEL|SYSTEM_ERROR|SYSTEM_DIALOG)(?![A-Za-z0-9_])");

    static Integer parse(String line) {
        java.util.regex.Matcher match = TYPE.matcher(line);

        if (!match.find()) {
            return null;
        }

        if ("SYSTEM_DIALOG".equals(match.group(1))) {
            return Integer.valueOf(2008);
        }

        if ("SYSTEM_ERROR".equals(match.group(1))) {
            return Integer.valueOf(2010);
        }

        if ("NAVIGATION_BAR_PANEL".equals(match.group(1))) {
            return Integer.valueOf(2024);
        }

        return "WALLPAPER".equals(match.group(1))
                ? Integer.valueOf(2013)
                : Integer.valueOf(match.group(1));
    }
}
