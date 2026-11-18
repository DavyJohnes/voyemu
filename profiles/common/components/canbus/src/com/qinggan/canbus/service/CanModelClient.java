package com.qinggan.canbus.service;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;

import java.io.Closeable;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/** CAN2 transport; the worker owns requests and the monitor observes only lease EOF. */
public final class CanModelClient implements Closeable {
    static final int MAGIC = 0x43414e32;
    private LocalSocket active, lease;
    private boolean closed;

    private static int[] request(
            LocalSocket socket, int op, int state, int value, long session, long epoch)
            throws IOException {
        byte[] request =
                ByteBuffer.allocate(32)
                        .order(ByteOrder.BIG_ENDIAN)
                        .putInt(MAGIC)
                        .putInt(op)
                        .putInt(state)
                        .putInt(value)
                        .putLong(session)
                        .putLong(epoch)
                        .array();

        socket.getOutputStream().write(request);
        byte[] response = new byte[49];

        if (socket.getInputStream().read(response) != 48) {
            throw new IOException("invalid response size");
        }

        ByteBuffer reply = ByteBuffer.wrap(response).order(ByteOrder.BIG_ENDIAN);
        int[] words = new int[12];

        for (int i = 0; i < words.length; i++) {
            words[i] = reply.getInt();
        }

        if (words[0] != MAGIC) {
            throw new IOException("invalid response magic");
        }

        return words;
    }

    private LocalSocket connect() throws IOException {
        LocalSocket socket = new LocalSocket(LocalSocket.SOCKET_SEQPACKET);

        synchronized (this) {
            if (closed) {
                socket.close();
                throw new IOException("transport closed");
            }

            active = socket;
        }

        try {
            // Android 9 creates LocalSocket's fd lazily; options require an existing fd.
            socket.getOutputStream();
            socket.setSoTimeout(500);
            socket.connect(
                    new LocalSocketAddress(
                            "/dev/socket/voyemu_can_model",
                            LocalSocketAddress.Namespace.FILESYSTEM));

            return socket;
        } catch (IOException | RuntimeException failure) {
            // checkstyle-allow IllegalCatch: any failed connect closes the socket and reports
            // IOException
            synchronized (this) {
                if (active == socket) {
                    active = null;
                }
            }

            socket.close();

            if (failure instanceof IOException) {
                throw (IOException) failure;
            }

            throw new IOException("socket unavailable", failure);
        }
    }

    public int[] exchange(int op, int state, int value, long session, long epoch)
            throws IOException {
        LocalSocket socket = connect();

        try {
            return request(socket, op, state, value, session, epoch);
        } finally {
            synchronized (this) {
                if (active == socket) {
                    active = null;
                }
            }

            socket.close();
        }
    }

    public void acquireLease(long session, long epoch) throws IOException {
        LocalSocket socket = connect();

        try {
            int[] reply = request(socket, 4, 0, 0, session, epoch);
            long rs = ((long) reply[6] << 32) | (reply[7] & 0xffffffffL);
            long re = ((long) reply[8] << 32) | (reply[9] & 0xffffffffL);

            if (reply[1] != 0 || rs != session || re != epoch) {
                throw new IOException("lease rejected");
            }

            socket.setSoTimeout(0);

            synchronized (this) {
                if (closed) {
                    throw new IOException("transport closed");
                }

                lease = socket;

                if (active == socket) {
                    active = null;
                }
            }

        } catch (IOException failure) {
            synchronized (this) {
                if (active == socket) {
                    active = null;
                }
            }

            socket.close();
            throw failure;
        }
    }

    public synchronized void watchLease(Runnable lost) throws IOException {
        if (closed || lease == null) {
            throw new IOException("lease unavailable");
        }

        LocalSocket socket = lease;
        new Thread(() -> monitor(socket, lost), "can-model-lease").start();
    }

    private void monitor(LocalSocket socket, Runnable lost) {
        try {
            socket.getInputStream().read();
        } catch (IOException failure) {
            /* EOF and errors both invalidate the source. */
        } finally {
            boolean notify;

            synchronized (this) {
                notify = !closed && lease == socket;

                if (lease == socket) {
                    lease = null;
                }
            }

            try {
                socket.close();
            } catch (IOException ignored) {
            }

            if (notify) {
                lost.run();
            }
        }
    }

    @Override
    public synchronized void close() {
        closed = true;

        if (active != null) {
            try {
                active.close();
            } catch (IOException ignored) {
            }
        }

        if (lease != null) {
            try {
                lease.shutdownInput();
            } catch (IOException ignored) {
            }

            try {
                lease.close();
            } catch (IOException ignored) {
            }
        }

        active = null;
        lease = null;
    }
}
