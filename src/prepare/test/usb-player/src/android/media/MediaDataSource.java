package android.media;

public abstract class MediaDataSource {
    public abstract int readAt(long p, byte[] b, int o, int n) throws java.io.IOException;

    public abstract long getSize() throws java.io.IOException;

    public abstract void close() throws java.io.IOException;
}
