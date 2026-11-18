package android.util;

import java.util.ArrayList;
import java.util.List;

public final class Log {
    public static final List<String> LINES = new ArrayList<>();

    public static int i(String tag, String text) {
        LINES.add(tag + ":" + text);

        return 0;
    }
}
