package android.net;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;

/** Android 9 lazily allocates the fd on stream access or connect, not construction. */
public final class LocalSocket {
    public static final int SOCKET_SEQPACKET = 3;
    public static int live;
    public static boolean failConnect, badPacket;
    private boolean created, connected, closed;
    private int timeout;
    private final ByteArrayOutputStream output = new ByteArrayOutputStream();

    public LocalSocket(int type) {
        if (type != SOCKET_SEQPACKET) {
            throw new AssertionError();
        }

        ++live;
    }

    public void setSoTimeout(int value) throws IOException {
        if (!created) {
            throw new IOException("socket not created");
        }

        timeout = value;
    }

    public void connect(LocalSocketAddress address) throws IOException {
        created = true;

        if (failConnect) {
            throw new IOException("connect failed");
        }

        if (timeout != 500 || !address.name.equals("/dev/socket/voyemu_can_model")) {
            throw new AssertionError("bounded fixed endpoint required");
        }

        connected = true;
    }

    public OutputStream getOutputStream() {
        created = true;

        return output;
    }

    public InputStream getInputStream() {
        created = true;

        return new InputStream() {
            public int read() {
                return -1;
            }

            public int read(byte[] into, int offset, int length) throws IOException {
                if (!connected || output.size() != 32) {
                    throw new IOException("request unavailable");
                }

                byte[] reply = ByteBuffer.allocate(badPacket ? 49 : 48).putInt(0x43414e32).array();
                System.arraycopy(reply, 0, into, offset, reply.length);

                return reply.length;
            }
        };
    }

    public void shutdownInput() throws IOException {}

    public void close() throws IOException {
        if (!closed) {
            closed = true;
            --live;
        }
    }
}
