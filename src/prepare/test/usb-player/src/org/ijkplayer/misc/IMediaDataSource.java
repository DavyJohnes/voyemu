package org.ijkplayer.misc;

public interface IMediaDataSource {
    int readAt(long p, byte[] b, int o, int n) throws java.io.IOException;

    long getSize() throws java.io.IOException;

    void close() throws java.io.IOException;
}
