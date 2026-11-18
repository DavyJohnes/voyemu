package org.ijkplayer.misc;

public interface IMediaDataSource {
    void close() throws java.io.IOException;

    long getSize() throws java.io.IOException;

    int readAt(long p0, byte[] p1, int p2, int p3) throws java.io.IOException;
}
