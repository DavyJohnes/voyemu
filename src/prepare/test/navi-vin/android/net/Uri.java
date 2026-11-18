package android.net;

public class Uri {
    public static Uri parse(String x) {
        if (!x.equals("content://com.voyemu.free731.identity")) {
            throw new AssertionError();
        }

        return new Uri();
    }
}
