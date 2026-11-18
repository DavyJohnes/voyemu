package com.qinggan.canbus.service;

import android.os.Binder;
import android.os.Bundle;
import android.os.Parcel;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Transactions 3..5 and 8..12 of qg.cluster.service, written as the OEM QGAPI proxy writes them,
 * reach the IPK link; cluster presentation requests reach the subscribers as setPresentationState.
 */
public final class ClusterInfoBinderTest {
    static void check(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    static Parcel arg() {
        Parcel p = Parcel.obtain();

        p.writeInterfaceToken(ClusterThemeBinder.DESCRIPTOR);

        return p;
    }

    /** writeParcelable of a QGAPI enum: creator name, ordinal and, for most enums, its value. */
    static void parcelEnum(Parcel p, int ordinal, boolean value) {
        p.writeString("com.qinggan.cluster.info.Enum");
        p.writeInt(ordinal);

        if (value) {
            p.writeInt(ordinal + 100);
        }
    }

    static Parcel media(int source, int state, String song, String artist) {
        Parcel p = arg();

        p.writeInt(1);
        parcelEnum(p, source, true);
        parcelEnum(p, 1, true);
        p.writeFloat(101.7f);
        parcelEnum(p, state, true);
        p.writeString(song);
        p.writeString(artist);
        p.writeString(null);

        return p;
    }

    static Parcel phone(String number, String name, int duration, int state, int vehicle) {
        Parcel p = arg();

        p.writeInt(1);
        p.writeString(number);
        p.writeString(name);
        p.writeInt(duration);
        parcelEnum(p, state, true);
        p.writeString("device");
        p.writeInt(1);
        p.writeInt(vehicle);
        p.writeInt(80);
        p.writeInt(4);
        p.writeInt(0);

        return p;
    }

    static Parcel navi(int guide, int distance, String current, String turn, int icon) {
        Parcel p = arg();

        p.writeInt(1);
        parcelEnum(p, guide, false);
        parcelEnum(p, 1, false);
        parcelEnum(p, 2, true);

        for (int value : new int[] {11, 12, 13}) {
            p.writeInt(value);
        }

        p.writeInt(distance);
        p.writeInt(60);
        p.writeInt(0);
        p.writeString(current);
        p.writeString(turn);
        p.writeInt(1);
        p.writeString("angle");
        p.writeInt(0);
        p.writeInt(0);
        p.writeString("destination");
        p.writeInt(0);
        p.writeInt(icon);

        return p;
    }

    static List<Integer> commands(IpkLink link, int from) {
        List<Integer> result = new ArrayList<>();
        List<byte[][]> pending = link.pendingFrames();

        for (int i = from; i < pending.size(); i++) {
            for (byte[] frame : pending.get(i)) {
                result.add(frame[5] & 255);
            }
        }

        return result;
    }

    static void transact(ClusterThemeBinder binder, int code, Parcel data) throws Exception {
        Parcel reply = Parcel.obtain();

        check(binder.transact(code, data, reply, 0), "transaction " + code);
        reply.readException();
    }

    static final class Presentation extends Binder {
        final List<String> seen = Collections.synchronizedList(new ArrayList<>());

        @Override
        protected boolean onTransact(int code, Parcel d, Parcel r, int flags) {
            if (code == 4) {
                return true;
            }

            check(code == 3 && flags == 1 && r == null, "oneway setPresentationState");
            d.enforceInterface(ClusterThemeBinder.CALLBACK);
            check("CONCERN_PRESENTATION".equals(d.readString()), "concern");
            seen.add(d.readString() + "=" + d.readInt());

            return true;
        }
    }

    static void information() throws Exception {
        CanBusStubService canbus = new CanBusStubService();
        ClusterThemeBinder binder = new ClusterThemeBinder(canbus);

        canbus.binder = binder;
        IpkLink link = canbus.ipk().link();
        transact(binder, 8, media(9, 1, "Песня", "Автор"));
        check(commands(link, 0).equals(List.of(0x6e, 0x6e)), "USB media: " + commands(link, 0));
        byte[] song = link.pendingFrames().get(0)[0];
        check(song[6] == 2 && song[7] == 2 && song[8] == 3 && song[12] == 1, "USB bytes");
        transact(binder, 8, media(1, 2, null, null));
        byte[] fm = link.pendingFrames().get(1)[0];
        check(fm[5] == 0x6f && fm[6] == 1 && fm[12] == 2 && fm[13] == 5, "paused FM station");
        transact(binder, 9, phone("+79990000001", "Иван", 0, 1, 1));
        byte[] call = link.pendingFrames().get(2)[0];
        check(call[5] == 0x6a && call[6] == 2 && call[7] == 1 && call[12] == 1, "incoming call");
        transact(binder, 9, phone("+79990000001", "", 65, 5, 0));
        byte[][] talk = link.pendingFrames().get(3);
        check(talk.length == 2 && talk[0][12] == 2 && talk[1][17] == 65, "number and duration");
        transact(binder, 11, navi(7, 0, "Тверская", null, 0));
        check(commands(link, 4).equals(List.of(0x68, 0x77)), "roaming: " + commands(link, 4));
        transact(binder, 11, navi(5, 1260, null, "Арбат", 3));
        check(commands(link, 6).equals(List.of(0x77, 0x68)), "guidance: " + commands(link, 6));
        Parcel lanes = arg();
        lanes.writeInt(1);
        lanes.writeInt(1);
        Bundle ids = new Bundle();
        ids.put("0", 0x21);
        lanes.writeBundle(ids);
        transact(binder, 12, lanes);
        Parcel hide = arg();
        hide.writeInt(0);
        hide.writeInt(0);
        transact(binder, 12, hide);
        check(commands(link, 8).equals(List.of(0x76, 0x76)), "lanes shown and hidden");
        check(link.pendingFrames().get(8)[0][8] == 0x21, "lane id");
        Parcel calls = arg();
        calls.writeInt(1);
        calls.writeBundle(new Bundle());
        transact(binder, 10, calls);
        transact(binder, 8, media(6, 1, "x", null));
        Parcel empty = arg();
        empty.writeInt(0);
        transact(binder, 9, empty);
        check(
                link.pendingFrames().size() == 10,
                "call list, phone source and null info send nothing");
        Parcel ready = arg();
        ready.writeString("qg.car.cluster.MEDIA");
        ready.writeInt(1);
        transact(binder, 3, ready);
        Parcel request = arg();
        request.writeString("qg.car.cluster.NAVI_FULL");
        request.writeInt(1);
        transact(binder, 4, request);
        Parcel show = arg();
        show.writeString("qg.car.cluster.MEDIA_SMALL");
        transact(binder, 5, show);
        check(commands(link, 10).equals(List.of(0x61, 0x60, 0x61)), "readiness frames");
        check(link.pendingFrames().get(10)[0][10] == 2, "media ready");
        check(link.pendingFrames().get(12)[0][11] == 1, "media ready to show");
    }

    static void presentation() throws Exception {
        try (IpkLinkTest.Ivcd ivcd = new IpkLinkTest.Ivcd()) {
            CanBusStubService canbus = new CanBusStubService();
            canbus.ipk = new IpkInfoSync(new IpkLink(ivcd.address(), 100, 400, 50, 200));
            ClusterThemeBinder binder = new ClusterThemeBinder(canbus);
            canbus.binder = binder;
            Presentation callback = new Presentation();
            Parcel register = arg();
            register.writeStrongBinder(callback);
            transact(binder, 19, register);
            canbus.ipk().link().start();
            long deadline = System.nanoTime() + 5_000_000_000L;

            while (ivcd.connections.isEmpty() && System.nanoTime() < deadline) {
                Thread.sleep(10);
            }

            ivcd.last().write(IpkLinkTest.reply(0x81, 1));
            ivcd.last().write(IpkLinkTest.reply(0x9b, 2));

            while (callback.seen.size() < 2 && System.nanoTime() < deadline) {
                Thread.sleep(10);
            }

            check(
                    callback.seen.equals(
                            List.of("qg.car.cluster.MEDIA_SMALL=1", "qg.car.cluster.REST=2")),
                    "presentation callbacks: " + callback.seen);
            canbus.ipk().link().stop();
        }
    }

    public static void main(String[] args) throws Exception {
        information();
        presentation();
        System.out.println("ClusterInfoBinderTest PASS");
    }
}
