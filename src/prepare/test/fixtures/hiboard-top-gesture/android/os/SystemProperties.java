package android.os;

public final class SystemProperties {
    public static final java.util.Map<String, String> VALUES = new java.util.HashMap<>();

    public static String get(String key) {
        return VALUES.getOrDefault(key, "");
    }
}
