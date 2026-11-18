package com.voyemu.api;

import android.os.IBinder;
import android.os.ParcelFileDescriptor;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;

/** Only fixed read commands; no public port, shell, or arbitrary Binder endpoint. */
final class ViewServer implements AutoCloseable {
    private Object manager;
    private int port;
    private volatile Socket updates;
    private volatile boolean closed;
    private java.util.function.Consumer<String> listener;

    void onInvalidation(java.util.function.Consumer<String> listener) {
        this.listener = listener;
    }

    static final class Window {
        final String hash, title;
        Integer display, ownerUid, type;
        Integer sessionPid, sessionUid, order;
        String sessionId, token, client;
        Boolean currentFocus, obscured;
        String ownerPackage;
        int[] frame;
        boolean transformed;
        Boolean touchable, visible, hasSurface, focusable, drawn, viewVisible;

        Window(String hash, String title) {
            this.hash = hash;
            this.title = title;
        }
    }

    private synchronized int start() throws Exception {
        if (closed) {
            throw new IllegalStateException("viewserver_closed");
        }

        if (port != 0) {
            return port;
        }

        GuestPhase.mark(
                GuestPhase.Phase.SERVER_START_BEGIN, android.os.SystemClock.uptimeMillis(), -1);
        Class<?> properties = Class.forName("android.os.SystemProperties");
        java.lang.reflect.Method get = properties.getMethod("get", String.class);
        String secure = (String) get.invoke(null, "ro.secure"),
                debug = (String) get.invoke(null, "ro.debuggable");

        if (!"0".equals(secure) && !"1".equals(debug)) {
            throw new IllegalStateException("viewserver_debug_gate");
        }

        Class<?> global = Class.forName("android.view.WindowManagerGlobal");
        manager = global.getMethod("getWindowManagerService").invoke(null);
        Class<?> type = Class.forName("android.view.IWindowManager");

        if ((Boolean) type.getMethod("isViewServerRunning").invoke(manager)) {
            throw new IllegalStateException("viewserver_owner_conflict");
        }

        // Do not attach to or stop an existing server. A bounded deterministic port set is
        // guest-only.
        for (int candidate = 4939; candidate < 4947; ++candidate) {
            if ((Boolean) type.getMethod("startViewServer", int.class).invoke(manager, candidate)) {
                port = candidate;
                watch();
                GuestPhase.mark(
                        GuestPhase.Phase.SERVER_START_END,
                        android.os.SystemClock.uptimeMillis(),
                        -1);

                return port;
            }
        }

        throw new IllegalStateException("viewserver_start_failed");
    }

    // A literal IPv4 loopback address needs no name resolution.
    @SuppressWarnings("AddressSelection")
    private void watch() {
        Thread watcher =
                new Thread(
                        () -> {
                            try (Socket socket = new Socket()) {
                                updates = socket;
                                socket.connect(new InetSocketAddress("127.0.0.1", port), 2000);
                                socket.setSoTimeout(1000);
                                socket.getOutputStream()
                                        .write("AUTOLIST\n".getBytes(StandardCharsets.US_ASCII));
                                InputStream input = socket.getInputStream();

                                while (!closed) {
                                    String line;

                                    try {
                                        line = updateLine(input);
                                    } catch (java.net.SocketTimeoutException timeout) {
                                        continue;
                                    }

                                    if (line == null) {
                                        break;
                                    }

                                    if (!"LIST UPDATE".equals(line)
                                            && !"ACTION_FOCUS UPDATE".equals(line)) {
                                        throw new IllegalStateException(
                                                "viewserver_autolist_format");
                                    }

                                    if (listener != null) {
                                        listener.accept(line);
                                    }
                                }
                            } catch (Exception error) {
                                // checkstyle-allow IllegalCatch: the watcher thread reports any end
                                // as a source gap
                                if (!closed && listener != null) {
                                    listener.accept("SOURCE GAP");
                                }
                            } finally {
                                updates = null;
                            }
                        },
                        "voyemu-viewserver-events");

        watcher.setDaemon(true);
        watcher.start();
    }

    private static String updateLine(InputStream input) throws Exception {
        StringBuilder line = new StringBuilder();

        for (; ; ) {
            int value = input.read();

            if (value < 0) {
                return null;
            }

            if (value == 10) {
                return line.toString();
            }

            if (value == 13) {
                continue;
            }

            if (line.length() >= 64 || value > 127) {
                throw new IllegalStateException("viewserver_autolist_format");
            }

            line.append((char) value);
        }
    }

    // A literal IPv4 loopback address needs no name resolution.
    @SuppressWarnings("AddressSelection")
    private String command(String command, long deadline, boolean interruptible) throws Exception {
        int ownedPort = start();
        int timeout =
                (int) Math.max(1, Math.min(5000, deadline - android.os.SystemClock.uptimeMillis()));

        try (Socket socket = new Socket()) {
            socket.connect(
                    new InetSocketAddress("127.0.0.1", ownedPort),
                    interruptible ? Math.min(timeout, 250) : timeout);
            socket.setSoTimeout(timeout);
            socket.getOutputStream().write((command + "\n").getBytes(StandardCharsets.US_ASCII));
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] buffer = new byte[4096];
            InputStream input = socket.getInputStream();
            String tail = "";

            for (; ; ) {
                if (android.os.SystemClock.uptimeMillis() >= deadline) {
                    throw new IllegalStateException("deadline");
                }

                int count =
                        interruptible
                                ? readInterruptibly(socket, input, buffer, deadline)
                                : input.read(buffer);

                if (count < 0) {
                    break;
                }

                if (bytes.size() + count > 3 * 1024 * 1024) {
                    throw new IllegalStateException("dump_limit");
                }

                bytes.write(buffer, 0, count);

                if (command.startsWith("DUMP ")) {
                    tail +=
                            new String(
                                    buffer,
                                    Math.max(0, count - 8),
                                    Math.min(8, count),
                                    StandardCharsets.US_ASCII);

                    if (tail.length() > 8) {
                        tail = tail.substring(tail.length() - 8);
                    }

                    if (ViewDump.terminal(tail)) {
                        String complete = ViewDump.complete(bytes.toByteArray());

                        if (complete != null) {
                            return complete;
                        }
                    }
                }
            }

            return StandardCharsets.UTF_8
                    .newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes.toByteArray()))
                    .toString();
        }
    }

    private static int readInterruptibly(
            Socket socket, InputStream input, byte[] buffer, long deadline) throws Exception {
        long readDeadline = Math.min(deadline, android.os.SystemClock.uptimeMillis() + 5000);

        for (; ; ) {
            long now = android.os.SystemClock.uptimeMillis();

            if (Thread.currentThread().isInterrupted() || now >= deadline) {
                throw new IllegalStateException("deadline");
            }

            if (now >= readDeadline) {
                throw new java.net.SocketTimeoutException();
            }

            socket.setSoTimeout((int) Math.min(250, readDeadline - now));

            try {
                return input.read(buffer);
            } catch (java.net.SocketTimeoutException pending) {
                /* Poll for the owned worker's cancellation. */
            }
        }
    }

    // Dropping trailing empty lines is intended.
    @SuppressWarnings("StringSplitter")
    Map<String, Window> list(long deadline) throws Exception {
        GuestPhase.mark(GuestPhase.Phase.LIST_BEGIN, android.os.SystemClock.uptimeMillis(), -1);
        String text = command("LIST", deadline, false);
        Map<String, Window> result = new LinkedHashMap<>();
        String[] lines = text.split("\\r?\\n");
        boolean done = false;

        for (String line : lines) {
            if ("DONE.".equals(line)) {
                done = true;
                continue;
            }

            if (done || result.size() >= 64) {
                throw new IllegalStateException("window_limit");
            }

            int separator = line.indexOf(' ');

            if (separator < 1 || !line.substring(0, separator).matches("[a-fA-F0-9]{1,8}")) {
                throw new IllegalStateException("window_list_format");
            }

            String hash = line.substring(0, separator).toLowerCase(java.util.Locale.ROOT);

            if (result.put(hash, new Window(hash, line.substring(separator + 1))) != null) {
                throw new IllegalStateException("window_duplicate");
            }
        }

        if (!done) {
            throw new IllegalStateException("window_list_incomplete");
        }

        GuestPhase.mark(GuestPhase.Phase.LIST_END, android.os.SystemClock.uptimeMillis(), -1);

        return result;
    }

    String dump(Window window, long deadline, boolean interruptible) throws Exception {
        if (!window.hash.matches("[a-f0-9]{1,8}")) {
            throw new IllegalStateException("window_identity");
        }

        return command("DUMP " + window.hash, deadline, interruptible);
    }

    // ByteArrayOutputStream.toString(Charset) needs API 33; the guest is API 28 or 30.
    @SuppressWarnings("JdkObsolete")
    static String metadata(long deadline) throws Exception {
        GuestPhase.mark(GuestPhase.Phase.WM_BEGIN, android.os.SystemClock.uptimeMillis(), -1);
        IBinder binder =
                (IBinder)
                        Class.forName("android.os.ServiceManager")
                                .getMethod("getService", String.class)
                                .invoke(null, "window");
        ParcelFileDescriptor[] pipe = ParcelFileDescriptor.createPipe();
        final Throwable[] failure = new Throwable[1];
        Thread writer =
                new Thread(
                        () -> {
                            try {
                                binder.dump(
                                        pipe[1].getFileDescriptor(),
                                        new String[] {"-a", "windows"});
                            } catch (Throwable error) {
                                // checkstyle-allow IllegalCatch: the dump thread hands any failure
                                // to its caller
                                failure[0] = error;
                            } finally {
                                try {
                                    pipe[1].close();
                                } catch (java.io.IOException ignored) {
                                    // Best-effort close of the write end after the dump.
                                }
                            }
                        },
                        "voyemu-wm-dump");
        writer.setDaemon(true);
        writer.start();
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();

        try (InputStream input = new ParcelFileDescriptor.AutoCloseInputStream(pipe[0])) {
            byte[] buffer = new byte[4096];
            int count;

            for (; ; ) {
                android.system.StructPollfd poll = new android.system.StructPollfd();
                poll.fd = pipe[0].getFileDescriptor();
                poll.events = (short) android.system.OsConstants.POLLIN;
                long remaining = deadline - android.os.SystemClock.uptimeMillis();

                if (remaining <= 0
                        || android.system.Os.poll(
                                        new android.system.StructPollfd[] {poll},
                                        (int) Math.min(remaining, 1000))
                                == 0) {
                    if (remaining <= 0) {
                        throw new IllegalStateException("deadline");
                    }

                    continue;
                }

                count = input.read(buffer);

                if (count < 0) {
                    break;
                }

                if (bytes.size() + count > 2 * 1024 * 1024) {
                    throw new IllegalStateException("wm_dump_limit");
                }

                bytes.write(buffer, 0, count);
            }
        }

        writer.join(1000);

        if (writer.isAlive() || failure[0] != null) {
            throw new IllegalStateException("wm_dump_failed");
        }

        GuestPhase.mark(GuestPhase.Phase.WM_END, android.os.SystemClock.uptimeMillis(), -1);

        return bytes.toString("UTF-8");
    }

    static void assignDisplays(Map<String, Window> windows, String metadata) {
        WindowMetadata.assignDisplays(windows, metadata);
    }

    static String identities(Map<String, Window> windows) {
        return WindowMetadata.identities(windows);
    }

    @Override
    public synchronized void close() {
        closed = true;

        if (updates != null) {
            try {
                updates.close();
            } catch (java.io.IOException ignored) {
                // Best-effort close while the server stops.
            }
        }

        if (port != 0) {
            try {
                Class.forName("android.view.IWindowManager")
                        .getMethod("stopViewServer")
                        .invoke(manager);
            } catch (ReflectiveOperationException ignored) {
                // The window manager may already have stopped its view server.
            }

            port = 0;
        }
    }
}
