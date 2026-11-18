package android.content;

import java.util.HashSet;
import java.util.Set;

public final class Intent {
    public static final String ACTION_MAIN = "android.intent.action.MAIN",
            CATEGORY_HOME = "android.intent.category.HOME",
            CATEGORY_DEFAULT = "android.intent.category.DEFAULT";
    public final String action;
    public final Set<String> categories = new HashSet<>();

    public Intent(String value) {
        action = value;
    }

    public Intent addCategory(String value) {
        categories.add(value);

        return this;
    }
}
