package com.voyemu.oem.audio;

import android.os.IBinder;
import android.os.IInterface;
import android.os.RemoteException;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

public class CapturePolicyTest {
    static final String
            REQ =
                    "requestAudioPolicyEx(IILandroid/os/IBinder;Lcom/qinggan/audiopolicy/IAudioPolicyCallback;Ljava/lang/String;Ljava/lang/String;II)I",
            AB =
                    "abandonAudioPolicy(Lcom/qinggan/audiopolicy/IAudioPolicyCallback;Ljava/lang/String;Z)I",
            REG =
                    "registerAudioClientCallback(Lcom/qinggan/audiopolicy/IAudioClientCallback;Ljava/lang/String;)Z",
            ID = "AudioPolicy_For_Record105";

    static class Token implements IBinder {
        List<DeathRecipient> deaths = new ArrayList<>();
        boolean dead, immediate;

        public void linkToDeath(DeathRecipient d, int f) throws RemoteException {
            if (dead) {
                throw new RemoteException();
            }

            deaths.add(d);

            if (immediate) {
                d.binderDied();
            }
        }

        public boolean unlinkToDeath(DeathRecipient d, int f) {
            return deaths.remove(d);
        }

        void kill() {
            dead = true;

            for (var d : new ArrayList<>(deaths)) {
                d.binderDied();
            }
        }
    }

    static class Callback implements IInterface {
        Token t = new Token();

        public IBinder asBinder() {
            return t;
        }
    }

    public static class BadListener implements com.qinggan.audiopolicy.IAudioClientCallback {
        Token t = new Token();
        int calls;

        public IBinder asBinder() {
            return t;
        }

        public void onAudioClientChange(com.qinggan.audiopolicy.AudioClient c) {
            calls++;
            throw new IllegalStateException("client failure");
        }
    }

    static class P implements OemAudioPolicy.Platform {
        boolean available = true;
        int created;

        public boolean captureAvailable() {
            return available;
        }

        public Object create(int u, int c, int g, int f, OemAudioPolicy.Change l) {
            created++;

            return new Object();
        }

        public int request(Object x) {
            return 1;
        }

        public int abandon(Object x) {
            return 1;
        }

        public Object volume(String s, int a, int b, int c) {
            return null;
        }
    }

    static class C implements OemAudioPolicy.Config {
        public boolean read() {
            return true;
        }

        public boolean commit(boolean x) {
            return true;
        }
    }

    static void check(boolean b) {
        if (!b) {
            throw new AssertionError("condition");
        }
    }

    static void eq(Object a, Object b) {
        if (!Objects.equals(a, b)) {
            throw new AssertionError(a + " != " + b);
        }
    }

    static Object[] args(Token t, Callback c) {
        return new Object[] {20, -1, t, c, ID, "com.qinggan.camera", 0, 105};
    }

    public static void main(String[] x) throws Exception {
        Token stub = new Token(), owner = new Token();
        Callback cb = new Callback(), foreign = new Callback();
        P p = new P();

        OemAudioPolicy.startForTest(stub, p, new C());
        Object[] a = args(owner, cb), other = args(new Token(), foreign);

        for (int field : new int[] {0, 1, 4, 5, 6, 7}) {
            Object[] bad = a.clone();
            bad[field] = field == 4 || field == 5 ? "foreign" : 999;
            eq(OemAudioPolicy.dispatch(stub, REQ, bad), 0);
        }

        String alias =
                "requestAudioFocus(Lcom/qinggan/audiopolicy/IAudioPolicyCallback;Ljava/lang/String;IILjava/lang/String;II)I";
        eq(
                OemAudioPolicy.dispatch(
                        stub,
                        alias,
                        new Object[] {cb, "alias-invalid", 1, 19, "com.qinggan.camera", 1, 0}),
                0);
        p.available = false;
        eq(OemAudioPolicy.dispatch(stub, REQ, a), 0);
        p.available = true;
        Token immediate = new Token();
        immediate.immediate = true;
        eq(OemAudioPolicy.dispatch(stub, REQ, args(immediate, new Callback())), 0);
        eq(OemAudioPolicy.dispatch(stub, REQ, a), 1);
        eq(p.created, 0);
        eq(OemAudioPolicy.dispatch(stub, REQ, a), 1);
        eq(OemAudioPolicy.dispatch(stub, REQ, other), 0);
        eq(OemAudioPolicy.dispatch(stub, AB, new Object[] {foreign, ID, true}), 0);
        BadListener badListener = new BadListener();
        eq(OemAudioPolicy.dispatch(stub, REG, new Object[] {badListener, "test"}), true);
        eq(OemAudioPolicy.dispatch(stub, AB, new Object[] {cb, ID, true}), 1);
        check(badListener.calls > 0);
        eq(OemAudioPolicy.dispatch(stub, REQ, a), 1);
        owner.kill();
        eq(OemAudioPolicy.dispatch(stub, REQ, other), 1);
        eq(OemAudioPolicy.dispatch(stub, AB, new Object[] {foreign, ID, true}), 1);
        Token t1 = new Token(), t2 = new Token();
        Callback c1 = new Callback(), c2 = new Callback();
        CountDownLatch ready = new CountDownLatch(2), go = new CountDownLatch(1);
        int[] got = new int[2];
        ExecutorService pool = Executors.newFixedThreadPool(2);
        Future<Integer>
                f1 =
                        pool.submit(() -> {
                            ready.countDown();
                            go.await();

                            return (Integer) OemAudioPolicy.dispatch(stub, REQ, args(t1, c1));
                        }),
                f2 =
                        pool.submit(() -> {
                            ready.countDown();
                            go.await();

                            return (Integer) OemAudioPolicy.dispatch(stub, REQ, args(t2, c2));
                        });
        ready.await();
        go.countDown();
        got[0] = f1.get();
        got[1] = f2.get();
        pool.shutdown();
        eq(got[0] + got[1], 1);
        eq(OemAudioPolicy.dispatch(stub, AB, new Object[] {got[0] == 1 ? c2 : c1, ID, true}), 0);
        eq(OemAudioPolicy.dispatch(stub, AB, new Object[] {got[0] == 1 ? c1 : c2, ID, true}), 1);
        eq(OemAudioPolicy.dispatch(stub, REQ, other), 1);
        OemAudioPolicy.stop(stub);
        eq(OemAudioPolicy.dispatch(stub, REQ, other), 0);
        System.out.println("PASS record scope busy ownership race release callback death stop");
    }
}
