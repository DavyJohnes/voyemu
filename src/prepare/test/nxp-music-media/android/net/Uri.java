package android.net;

public final class Uri {
    private final String text;

    private Uri(String s) {
        text = s;
    }

    public static Uri parse(String s) {
        return new Uri(s);
    }

    public String getPath() {
        return java.net.URI.create(text).getPath();
    }

    public String toString() {
        return text;
    }
}
