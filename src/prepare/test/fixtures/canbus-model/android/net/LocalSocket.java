package android.net;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.util.HashMap;
import java.util.Map;

/** CAN2 model double; an accepted ACC command applies at the next snapshot like the model queue. */
public final class LocalSocket {
    public static final int SOCKET_SEQPACKET = 3;
    private static final int MAGIC = 0x43414e32, NONE = -2;
    private static final Map<String, Integer> CONNECTS = new HashMap<>();
    private static final int[] REQUESTS = new int[5];
    private static boolean available = true, holdAcc, rejectAcc;
    private static long session = 1, epoch = 1, revision = 1;
    private static int acc = 2, pendingAcc = NONE;
    private static LocalSocket lease;
    private final ByteArrayOutputStream request = new ByteArrayOutputStream();
    private boolean created, closed;
    private int timeout;

    public LocalSocket(int type) {
        if (type != SOCKET_SEQPACKET) {
            throw new AssertionError("SEQPACKET required");
        }
    }

    public static synchronized void available(boolean value) {
        available = value;
    }

    public static synchronized void holdAcc(boolean value) {
        holdAcc = value;
    }

    public static synchronized void rejectAcc(boolean value) {
        rejectAcc = value;
    }

    public static synchronized int acc() {
        return acc;
    }

    public static synchronized int pendingAcc() {
        return pendingAcc;
    }

    public static synchronized int requests(int op) {
        return REQUESTS[op];
    }

    public static synchronized int connectsFrom(String thread) {
        return CONNECTS.getOrDefault(thread, 0);
    }

    public static synchronized long[] identity() {
        return new long[] {session, epoch, revision};
    }

    /** Model source loss: ACC becomes unknown in a new epoch. */
    public static synchronized void loseSource() {
        acc = -1;
        pendingAcc = NONE;
        epoch++;
        revision++;
    }

    /** Model restart: a new session with ACC ON; the old lease reaches EOF. */
    public static synchronized void restart() {
        session++;
        epoch = 1;
        revision = 1;
        acc = 2;
        pendingAcc = NONE;

        if (lease != null) {
            lease.release();
            lease = null;
        }
    }

    private static synchronized byte[] answer(LocalSocket socket, ByteBuffer in) {
        in.getInt();
        int op = in.getInt(), state = in.getInt(), value = in.getInt();
        long s = in.getLong(), e = in.getLong();
        boolean current = s == session && e == epoch;
        int status = 0;

        REQUESTS[op]++;

        if (op == 1) {
            applyPending();
        } else if (op == 3) {
            status = !current ? -2 : rejectAcc || state != 0 || (acc == -1 && value != 0) ? -1 : 0;

            if (status == 0) {
                pendingAcc = value;
            }
        } else if (op == 4) {
            status = current ? 0 : -2;

            if (status == 0) {
                lease = socket;
            }
        } else {
            status = -1;
        }

        return ByteBuffer.allocate(48)
                .putInt(MAGIC)
                .putInt(status)
                .putInt(acc == 2 ? 2 : -1)
                .putInt(acc == 2 ? 1 : -1)
                .putInt(-1)
                .putInt(acc)
                .putLong(session)
                .putLong(epoch)
                .putLong(revision)
                .array();
    }

    private static void applyPending() {
        if (holdAcc || pendingAcc == NONE) {
            return;
        }

        if (pendingAcc != acc) {
            acc = pendingAcc;
            epoch += acc == 2 ? 1 : 0;
            revision++;
        }

        pendingAcc = NONE;
    }

    public void setSoTimeout(int value) throws IOException {
        if (!created) {
            throw new IOException("socket not created");
        }

        timeout = value;
    }

    public void connect(LocalSocketAddress address) throws IOException {
        synchronized (LocalSocket.class) {
            CONNECTS.merge(Thread.currentThread().getName(), 1, Integer::sum);

            if (!available) {
                throw new IOException("model unavailable");
            }
        }

        if (timeout != 500 || !address.name.equals("/dev/socket/voyemu_can_model")) {
            throw new AssertionError("bounded fixed endpoint required");
        }
    }

    public OutputStream getOutputStream() {
        created = true;

        return request;
    }

    public InputStream getInputStream() {
        created = true;

        return new InputStream() {
            @Override
            public int read() throws IOException {
                synchronized (LocalSocket.this) {
                    while (!closed) {
                        try {
                            LocalSocket.this.wait();
                        } catch (InterruptedException stopped) {
                            Thread.currentThread().interrupt();
                            throw new IOException(stopped);
                        }
                    }
                }

                return -1;
            }

            @Override
            public int read(byte[] target, int offset, int length) throws IOException {
                if (request.size() != 32 || length < 48) {
                    throw new IOException("invalid transport request");
                }

                byte[] reply = answer(LocalSocket.this, ByteBuffer.wrap(request.toByteArray()));

                System.arraycopy(reply, 0, target, offset, reply.length);

                return reply.length;
            }
        };
    }

    public void shutdownInput() throws IOException {
        release();
    }

    public void close() throws IOException {
        release();
    }

    private synchronized void release() {
        closed = true;
        notifyAll();
    }
}
