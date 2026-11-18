package android.net;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;

/** Only the Android socket seam is substituted; responses come from production C model code. */
public final class LocalSocket {
    public static final int SOCKET_SEQPACKET = 3;
    private static Process backend;
    private static LocalSocket lease;
    private final ByteArrayOutputStream request = new ByteArrayOutputStream();
    private boolean created, connected, closed, isLease;
    private int timeout;

    public LocalSocket(int type) {
        if (type != SOCKET_SEQPACKET) {
            throw new AssertionError();
        }
    }

    public void setSoTimeout(int value) throws IOException {
        if (!created) {
            throw new IOException("socket not created");
        }

        timeout = value;
    }

    public void connect(LocalSocketAddress address) throws IOException {
        if (timeout != 500 || !address.name.equals("/dev/socket/voyemu_can_model")) {
            throw new AssertionError("bounded fixed endpoint required");
        }

        connected = true;
    }

    public OutputStream getOutputStream() {
        created = true;

        return request;
    }

    public InputStream getInputStream() {
        created = true;

        return new InputStream() {
            public int read() throws IOException {
                synchronized (LocalSocket.this) {
                    while (!closed) {
                        try {
                            LocalSocket.this.wait();
                        } catch (InterruptedException e) {
                            Thread.currentThread().interrupt();
                            throw new IOException(e);
                        }
                    }

                    return -1;
                }
            }

            public int read(byte[] target, int offset, int length) throws IOException {
                if (!connected || request.size() != 32 || length < 48) {
                    throw new IOException("invalid transport request");
                }

                byte[] wire = request.toByteArray();
                byte[] reply = exchange(0, wire);

                if (ByteBuffer.wrap(wire).getInt(4) == 4 && ByteBuffer.wrap(reply).getInt(4) == 0) {
                    isLease = true;
                    lease = LocalSocket.this;
                }

                System.arraycopy(reply, 0, target, offset, reply.length);

                return reply.length;
            }
        };
    }

    public static synchronized byte[] exchange(int action, byte[] request) throws IOException {
        if (backend == null) {
            backend =
                    new ProcessBuilder(System.getProperty("can.broker"))
                            .redirectError(ProcessBuilder.Redirect.INHERIT)
                            .start();
        }

        OutputStream out = backend.getOutputStream();
        out.write(action);
        out.write(request);
        out.flush();
        byte[] reply = backend.getInputStream().readNBytes(48);

        if (reply.length != 48) {
            throw new IOException("native model broker terminated");
        }

        return reply;
    }

    public static void nativeWrite(byte[] frame) throws IOException {
        byte[] request = new byte[32];

        System.arraycopy(frame, 0, request, 0, frame.length);
        exchange(1, request);
    }

    public static void loseSource() throws IOException {
        exchange(2, new byte[32]);
        LocalSocket old = lease;

        if (old != null) {
            old.close();
        }
    }

    public static synchronized void finish() throws Exception {
        if (backend != null) {
            backend.getOutputStream().close();

            if (!backend.waitFor(2, java.util.concurrent.TimeUnit.SECONDS)) {
                backend.destroyForcibly();
                throw new AssertionError("broker failed to stop");
            }

            if (backend.exitValue() != 0) {
                throw new AssertionError("broker exit " + backend.exitValue());
            }

            backend = null;
        }
    }

    public void shutdownInput() throws IOException {
        close();
    }

    public synchronized void close() throws IOException {
        closed = true;
        notifyAll();
    }
}
