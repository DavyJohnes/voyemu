package android.os;

public final class SystemProperties {
    public static String token = "0123456789abcdef0123456789abcdef";

    public static String get(String name) {
        if (!"qemu.voyemu.token".equals(name)) {
            throw new AssertionError(name);
        }

        return token;
    }
}
