package android.net;

public final class LocalSocketAddress {
    public enum Namespace {
        FILESYSTEM
    }

    public final String name;

    public LocalSocketAddress(String name, Namespace namespace) {
        if (namespace != Namespace.FILESYSTEM) {
            throw new AssertionError();
        }

        this.name = name;
    }
}
