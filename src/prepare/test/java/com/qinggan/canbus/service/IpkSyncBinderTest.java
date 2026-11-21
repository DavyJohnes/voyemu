package com.qinggan.canbus.service;

import android.os.Binder;
import android.os.Parcel;

import java.util.Arrays;
import java.util.List;

/**
 * syncMenuCardInfo of getIpkSyncSvr, written as the Settings copy of the QGAPI proxy writes it,
 * reaches the IPK link as the OEM 0x75 frame; the other operations send nothing and answer as
 * IpkSyncManager does without the service.
 */
public final class IpkSyncBinderTest {
    static void check(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    static Parcel transact(IpkSyncBinder binder, int code, Parcel data) throws Exception {
        Parcel reply = Parcel.obtain();

        check(binder.transact(code, data, reply, 0), "transaction " + code);
        reply.readException();

        return reply;
    }

    static Parcel arg(int... values) {
        Parcel p = Parcel.obtain();

        p.writeInterfaceToken(IpkSyncBinder.DESCRIPTOR);

        for (int value : values) {
            p.writeInt(value);
        }

        return p;
    }

    public static void main(String[] args) throws Exception {
        IpkInfoSync ipk =
                new IpkInfoSync(
                        new IpkLink(
                                new java.net.InetSocketAddress(
                                        java.net.InetAddress.getLoopbackAddress(), 9)));
        IpkSyncBinder binder = new IpkSyncBinder(ipk);
        IpkLink link = ipk.link();

        check(binder.asBinder() == binder, "binder of getIpkSyncSvr");
        Parcel descriptor = Parcel.obtain();
        check(binder.transact(Binder.INTERFACE_TRANSACTION, Parcel.obtain(), descriptor, 0), "");
        check(IpkSyncBinder.DESCRIPTOR.equals(descriptor.readString()), "interface descriptor");
        transact(
                binder,
                45,
                ClusterInfoBinderTest.cards(
                        IpkSyncBinder.DESCRIPTOR, 11, 5, 7, 0, 3, 1, 4, 2, 9, 3, 10, 4));
        List<byte[][]> pending = link.pendingFrames();
        check(pending.size() == 1, "one cards frame");
        check(
                Arrays.equals(
                        pending.get(0)[0],
                        new byte[] {10, 16, 0, 0, 8, 0x75, 6, 7, 3, 4, 9, 10, 11, 0, 13}),
                "OEM default cards in menu order");
        transact(binder, 45, ClusterInfoBinderTest.cards(IpkSyncBinder.DESCRIPTOR));
        check(
                Arrays.equals(
                        link.pendingFrames().get(1)[0],
                        new byte[] {10, 16, 0, 0, 2, 0x75, 0, 0, 13}),
                "no card selected");

        for (int code : new int[] {31, 32, 40}) {
            check(transact(binder, code, arg(0)).readInt() == -1, "default answer " + code);
        }

        transact(binder, 16, arg(2, 2001));
        transact(binder, 23, arg());
        check(link.pendingFrames().size() == 2, "other operations send nothing");
        check(!binder.transact(49, arg(), Parcel.obtain(), 0), "unknown transaction");
        System.out.println("IpkSyncBinderTest PASS");
    }
}
