package com.voyemu.api;

public final class ViewDumpFrameTest {
    private static void require(boolean condition) {
        if (!condition) {
            throw new AssertionError();
        }
    }

    private static byte[] bytes(String text) {
        return text.getBytes(java.nio.charset.StandardCharsets.UTF_8);
    }

    // A literal IPv4 loopback address needs no name resolution.
    @SuppressWarnings("AddressSelection")
    public static void main(String[] args) throws Exception {
        // Pinned API30 Resources.Theme exports adjacent resource-name keys.
        String dump =
                "android.View@a theme:android:style/Theme.Material()=6,forced mLeft=1,0 \nDONE.\n";

        require(
                ViewDump.parse(dump)
                        .get(0)
                        .properties
                        .get("theme:android:style/Theme.Material()")
                        .equals("forced"));
        require(
                ViewDump.parse("android.View@a layout:mLeft=1,0 layout:mLeft=1,0 \nDONE.\n")
                        .get(0)
                        .properties
                        .get("layout:mLeft")
                        .equals("0"));
        require(ViewDump.complete(bytes(dump)).equals(dump));
        require(ViewDump.complete(bytes(dump + "DONE\n")).equals(dump + "DONE\n"));
        require(ViewDump.complete(bytes("DONE\n")) == null);
        require(ViewDump.complete(bytes(dump + "extra\nDONE\n")) == null);
        require(ViewDump.complete(bytes("android.View@a text=99,x \nDONE.\n")) == null);
        String embedded = "x\nDONE.\n";
        String prefix = "android.View@a text=" + (embedded.length() + 2) + "," + embedded;
        require(ViewDump.complete(bytes(prefix)) == null);
        require(ViewDump.complete(bytes(prefix + "😀 \nDONE.\n")) != null);
        byte[] utf8 = bytes("android.View@a text=2,😀 \nDONE.\n");

        for (int end = 0; end < utf8.length; ++end) {
            require(ViewDump.complete(java.util.Arrays.copyOf(utf8, end)) == null);
        }

        require(ViewDump.complete(utf8) != null);

        try {
            ViewDump.complete(new byte[] {(byte) 0xff, 10, 68, 79, 78, 69, 46, 10});
            throw new AssertionError();
        } catch (java.nio.charset.CharacterCodingException expected) {
            // A malformed frame must be rejected.
        }

        try (java.net.ServerSocket server =
                new java.net.ServerSocket(0, 1, java.net.InetAddress.getLoopbackAddress())) {
            Thread writer =
                    new Thread(() -> {
                        try (java.net.Socket client = server.accept()) {
                            client.getOutputStream()
                                    .write(bytes(dump.substring(0, dump.length() - 2)));
                            Thread.sleep(20);
                            client.getOutputStream()
                                    .write(bytes(dump.substring(dump.length() - 2)));
                            Thread.sleep(
                                    500); // Keep the descriptor open after a complete hierarchy.
                        } catch (java.io.IOException | InterruptedException error) {
                            throw new RuntimeException(error);
                        }
                    });
            writer.start();

            try (java.net.Socket client = new java.net.Socket("127.0.0.1", server.getLocalPort())) {
                client.setSoTimeout(250);
                java.io.ByteArrayOutputStream received = new java.io.ByteArrayOutputStream();
                byte[] buffer = new byte[31];
                String complete = null;

                while (complete == null) {
                    int count = client.getInputStream().read(buffer);
                    require(count > 0);
                    received.write(buffer, 0, count);
                    complete = ViewDump.complete(received.toByteArray());
                }

                require(complete.equals(dump));
            }

            writer.join(1000);
            require(!writer.isAlive());
        }

        System.out.println("View dump resource-key and framing tests passed");
    }
}
