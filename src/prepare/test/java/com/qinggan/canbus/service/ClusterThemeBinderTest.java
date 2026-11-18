package com.qinggan.canbus.service;

import android.content.Context;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.ServiceManager;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class ClusterThemeBinderTest {
    static void check(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    static Parcel arg() {
        Parcel p = Parcel.obtain();

        p.writeInterfaceToken(ClusterThemeBinder.DESCRIPTOR);

        return p;
    }

    static Parcel state(int ordinal, int value) {
        Parcel p = arg();

        p.writeInt(1);
        p.writeInt(ordinal);
        p.writeInt(value);

        return p;
    }

    static int get(ClusterThemeBinder b, int ordinal, int value) throws Exception {
        Parcel r = Parcel.obtain();

        check(b.transact(7, state(ordinal, value), r, 0));
        r.readException();

        return r.readInt();
    }

    static class Callback extends Binder {
        final List<Integer> seen = Collections.synchronizedList(new ArrayList<>());

        protected boolean onTransact(int code, Parcel d, Parcel r, int flags) {
            check(code == 4 && flags == 1 && r == null);
            d.enforceInterface(ClusterThemeBinder.CALLBACK);
            check("CONCERN_CLUSTER_STATE".equals(d.readString()));
            check(d.readInt() == 1);
            int ordinal = d.readInt(), v = d.readInt();
            check(ordinal == 8 && v == 9 || ordinal == 27 && v == 28);
            seen.add(ordinal);
            seen.add(d.readInt());

            return true;
        }
    }

    static final class BlockingCallback extends Callback {
        volatile boolean armed;
        final CountDownLatch entered = new CountDownLatch(1), release = new CountDownLatch(1);

        protected boolean onTransact(int code, Parcel d, Parcel r, int flags) {
            if (armed) {
                armed = false;
                entered.countDown();

                try {
                    if (!release.await(5, TimeUnit.SECONDS)) {
                        throw new AssertionError("release timeout");
                    }
                } catch (InterruptedException e) {
                    throw new AssertionError(e);
                }
            }

            return super.onTransact(code, d, r, flags);
        }
    }

    static void interleaving() throws Exception {
        CanBusStubService can = new CanBusStubService();
        ClusterThemeBinder binder = new ClusterThemeBinder(can);

        can.binder = binder;
        BlockingCallback cb = new BlockingCallback();
        Parcel reg = arg();
        reg.writeStrongBinder(cb);
        binder.transact(19, reg, Parcel.obtain(), 0);
        cb.armed = true;
        java.util.concurrent.atomic.AtomicReference<Throwable> failure =
                new java.util.concurrent.atomic.AtomicReference<>();
        Thread first =
                new Thread(() -> {
                    try {
                        Parcel p = state(27, 28);
                        p.writeInt(1);
                        binder.transact(6, p, Parcel.obtain(), 0);
                    } catch (Throwable e) {
                        failure.set(e);
                    }
                });
        first.start();
        check(cb.entered.await(5, TimeUnit.SECONDS));
        can.applied = new CountDownLatch(1);
        Thread second =
                new Thread(() -> {
                    try {
                        Parcel p = state(27, 28);
                        p.writeInt(3001);
                        binder.transact(6, p, Parcel.obtain(), 0);
                    } catch (Throwable e) {
                        failure.set(e);
                    }
                });
        second.start();
        check(can.applied.await(5, TimeUnit.SECONDS));
        CountDownLatch registered = new CountDownLatch(1);
        Callback late =
                new Callback() {
                    @Override
                    public void linkToDeath(IBinder.DeathRecipient d, int flags) {
                        registered.countDown();
                    }
                };
        Thread register =
                new Thread(() -> {
                    try {
                        Parcel p = arg();
                        p.writeStrongBinder(late);
                        binder.transact(19, p, Parcel.obtain(), 0);
                    } catch (Throwable e) {
                        failure.set(e);
                    }
                });
        register.start();
        check(registered.await(5, TimeUnit.SECONDS));
        cb.release.countDown();
        first.join(5000);
        second.join(5000);
        register.join(5000);
        check(
                !first.isAlive()
                        && !second.isAlive()
                        && !register.isAlive()
                        && failure.get() == null);

        synchronized (cb.seen) {
            check(cb.seen.size() >= 8 && cb.seen.size() % 4 == 0);

            for (int i = 0; i < cb.seen.size(); i += 4) {
                check(cb.seen.get(i) == 8 && cb.seen.get(i + 2) == 27);
                check(cb.seen.get(i + 1) == (cb.seen.get(i + 3) + 999) / 1000);
            }

            check(cb.seen.get(cb.seen.size() - 1) == can.theme.skin());
        }

        synchronized (late.seen) {
            check(late.seen.size() >= 4 && late.seen.size() % 4 == 0);

            for (int i = 0; i < late.seen.size(); i += 4) {
                check(
                        late.seen.get(i) == 8
                                && late.seen.get(i + 2) == 27
                                && late.seen.get(i + 1) == (late.seen.get(i + 3) + 999) / 1000);
            }

            check(late.seen.get(late.seen.size() - 1) == can.theme.skin());
        }
    }

    static ClusterThemeBinder register(ClusterTheme theme, CanBusStubService can) {
        theme.register(can);

        return (ClusterThemeBinder) ServiceManager.SERVICES.get("qg.cluster.service");
    }

    static void recreateRegistration() throws Exception {
        CanBusStubService can = new CanBusStubService();
        ClusterTheme theme = new ClusterTheme(new Context());
        ClusterThemeBinder first = register(theme, can);

        can.binder = first;
        Callback subscriber = new Callback();
        Parcel request = arg();
        request.writeStrongBinder(subscriber);
        first.transact(19, request, Parcel.obtain(), 0);
        ClusterThemeBinder recreated = register(theme, can);
        can.binder = recreated;
        check(first == recreated);
        int before = subscriber.seen.size();
        Parcel set = state(27, 28);
        set.writeInt(1);
        recreated.transact(6, set, Parcel.obtain(), 0);
        check(
                subscriber.seen.size() == before + 4
                        && subscriber.seen.get(subscriber.seen.size() - 1) == 1);
        ClusterTheme other = new ClusterTheme(new Context());
        check(register(other, new CanBusStubService()) != recreated);
        check(register(theme, can) == recreated);
    }

    public static void main(String[] args) throws Exception {
        recreateRegistration();
        interleaving();
        CanBusStubService can = new CanBusStubService();
        ClusterThemeBinder b = new ClusterThemeBinder(can);
        can.binder = b;
        check(get(b, 27, 28) == 1001 && get(b, 8, 9) == 2);
        Parcel connect = Parcel.obtain();
        check(b.transact(1, arg(), connect, 0));
        connect.readException();
        check(connect.readInt() == 1);
        Callback cb = new Callback();
        Parcel reg = arg(), reply = Parcel.obtain();
        reg.writeStrongBinder(cb);
        check(b.transact(19, reg, reply, 0));
        reply.readException();
        check(cb.seen.equals(List.of(8, 2, 27, 1001)));
        Parcel set = state(27, 28);
        set.writeInt(1);
        reply = Parcel.obtain();
        check(b.transact(6, set, reply, 0));
        reply.readException();
        check(get(b, 27, 28) == 1 && get(b, 8, 9) == 1);
        check(cb.seen.subList(4, 8).equals(List.of(8, 1, 27, 1)));
        set = state(27, 28);
        set.writeInt(3002);
        b.transact(6, set, Parcel.obtain(), 0);
        check(get(b, 8, 9) == 4 && get(b, 27, 28) == 3002);
        Parcel unreg = arg();
        unreg.writeStrongBinder(cb);
        b.transact(20, unreg, Parcel.obtain(), 0);
        int count = cb.seen.size();
        set = state(8, 9);
        set.writeInt(2);
        b.transact(6, set, Parcel.obtain(), 0);
        check(cb.seen.size() == count && get(b, 27, 28) == 1001);
        check(get(b, 7, 8) == -1);
        Callback nonThemeCb = new Callback();
        reg = arg();
        reg.writeStrongBinder(nonThemeCb);
        b.transact(19, reg, Parcel.obtain(), 0);
        int before = nonThemeCb.seen.size(),
                skin = get(b, 27, 28),
                mode = get(b, 8, 9),
                rejected = android.util.Log.rejected;
        Parcel nonTheme = state(7, 8);
        nonTheme.writeInt(3);
        reply = Parcel.obtain();
        check(b.transact(6, nonTheme, reply, 0));
        reply.readException();
        check(
                nonThemeCb.seen.size() == before
                        && get(b, 27, 28) == skin
                        && get(b, 8, 9) == mode
                        && android.util.Log.rejected == rejected + 1);

        try {
            get(b, 27, 29);
            throw new AssertionError();
        } catch (IllegalArgumentException expected) {
        }

        reply = Parcel.obtain();
        check(b.transact(13, arg(), reply, 0));
        reply.readException();
        check(android.util.Log.rejected == rejected + 2 && nonThemeCb.seen.size() == before);
        System.out.println("ClusterThemeBinderTest PASS");
    }
}
