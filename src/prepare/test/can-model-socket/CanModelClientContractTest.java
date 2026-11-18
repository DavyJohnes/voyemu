import android.net.LocalSocket;

import com.qinggan.canbus.service.CanModelClient;

import java.io.IOException;

public final class CanModelClientContractTest {
    static void check(boolean condition) {
        if (!condition) {
            throw new AssertionError();
        }
    }

    public static void main(String[] args) throws Exception {
        CanModelClient client = new CanModelClient();

        check(client.exchange(1, 0, 0, 0, 0)[0] == 0x43414e32);
        check(LocalSocket.live == 0);
        LocalSocket.failConnect = true;

        try {
            client.exchange(1, 0, 0, 0, 0);
            throw new AssertionError();
        } catch (IOException expected) {
        }

        check(LocalSocket.live == 0);
        LocalSocket.failConnect = false;
        LocalSocket.badPacket = true;

        try {
            client.exchange(1, 0, 0, 0, 0);
            throw new AssertionError();
        } catch (IOException expected) {
        }

        check(LocalSocket.live == 0);
        LocalSocket.badPacket = false;
        client.close();

        try {
            client.exchange(1, 0, 0, 0, 0);
            throw new AssertionError();
        } catch (IOException expected) {
        }

        check(LocalSocket.live == 0);
        System.out.println(
                "Android lazy-fd ordering, bounded connect, framing and failure cleanup passed");
    }
}
