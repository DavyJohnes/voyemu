package com.qinggan.canbus.service;

import android.os.Bundle;

import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

/**
 * The head-unit information state shared by the A9 and A11 entries: the OEM source selection of
 * media, roaming and guidance, lanes, readiness and the cluster presentation requests.
 */
public final class IpkInfoSyncTest {
    private static void check(boolean condition, String message) {
        if (!condition) {
            throw new AssertionError(message);
        }
    }

    /** Frames queued since the previous call. */
    static final class Queue {
        final IpkLink link =
                new IpkLink(new InetSocketAddress(InetAddress.getLoopbackAddress(), 9));
        final IpkInfoSync sync = new IpkInfoSync(link);
        int seen;

        List<byte[]> take() {
            List<byte[]> result = new ArrayList<>();
            List<byte[][]> pending = link.pendingFrames();

            for (int i = seen; i < pending.size(); i++) {
                Collections.addAll(result, pending.get(i));
            }

            seen = pending.size();

            return result;
        }
    }

    private static String text(byte[] frame) {
        return new String(frame, 15, frame[13] & 255, StandardCharsets.UTF_8);
    }

    static void media() {
        Queue q = new Queue();

        q.sync.media("test", "USB", "PLAY", 0, "Песня", "Автор", null);
        List<byte[]> usb = q.take();
        check(usb.size() == 2 && usb.get(0)[5] == 0x6e, "USB song and artist");
        check(usb.get(0)[6] == 2 && usb.get(0)[7] == 2 && usb.get(0)[8] == 3, "USB bytes");
        check(text(usb.get(0)).equals("Песня") && text(usb.get(1)).equals("Автор"), "USB text");
        q.sync.media("test", "USB", "STOP", 0, "Песня", "Автор", null);
        check(q.take().isEmpty(), "stopped USB is not shown");
        q.sync.media("test", "NetworkMusic", "PLAY", 0, null, null, null);
        List<byte[]> network = q.take();
        check(network.size() == 1 && network.get(0)[6] == 4, "network music artist only");
        check(network.get(0)[7] == 2 && network.get(0)[8] == 1, "device persists, no song");
        q.sync.media("test", "A2DP", "STOP", 0, "蓝牙音乐", "", null);
        List<byte[]> bluetooth = q.take();
        check(bluetooth.size() == 2 && bluetooth.get(0)[8] == 2, "Bluetooth placeholder");
        check(bluetooth.get(0)[6] == 3 && bluetooth.get(0)[7] == 6, "Bluetooth bytes");
        q.sync.media("test", "FM", "PAUSE", 101.7f, "正在搜索电台", null, null);
        List<byte[]> fm = q.take();
        check(fm.size() == 1 && fm.get(0)[5] == 0x6f && fm.get(0)[6] == 1, "FM station");
        check(fm.get(0)[7] == 1 && fm.get(0)[12] == 2 && text(fm.get(0)).equals("101.7"), "FM");
        q.sync.media("test", "NetworkRadio", "PLAY", 0, "", null, null);
        List<byte[]> radio = q.take();
        check(radio.size() == 2 && radio.get(0)[6] == 3 && radio.get(0)[12] == 5, "radio");
        check(text(radio.get(0)).equals("未知") && radio.get(1)[12] == 1, "unknown names");
        q.sync.media("test", "Phone", "PLAY", 0, "x", null, null);
        q.sync.media("test", null, "PLAY", 0, "x", null, null);
        q.sync.media("test", "Other", "PLAY", 0, "x", null, null);
        check(q.take().isEmpty(), "phone, missing and other sources send nothing");
    }

    static void navigation() {
        Locale previous = Locale.getDefault();

        try {
            Locale.setDefault(Locale.US);
            Queue q = new Queue();
            q.sync.navi("test", "ROAM", 0, 0, null, "Тверская");
            List<byte[]> roam = q.take();
            check(roam.size() == 2 && roam.get(0)[5] == 0x68 && roam.get(0)[6] == 2, "TBT off");
            check(roam.get(1)[5] == 0x77 && text(roam.get(1)).equals("Тверская"), "road");
            q.sync.navi("test", "ROAM", 0, 0, null, "Тверская");
            check(q.take().isEmpty(), "same road is not repeated");
            q.sync.navi("test", "ROAM", 0, 0, null, "Арбат");
            check(q.take().size() == 1, "changed road");
            q.sync.navi("test", "GUIDING", 3, 1260, "Арбат", null);
            List<byte[]> guide = q.take();
            check(guide.size() == 2 && guide.get(0)[5] == 0x77 && guide.get(0)[6] == 2, "off");
            byte[] tbt = guide.get(1);
            check(tbt[5] == 0x68 && tbt[6] == 1 && tbt[8] == 3, "guidance icon");
            check(
                    new String(tbt, 15, tbt[11], StandardCharsets.UTF_8).equals("1.2")
                            && new String(tbt, 15 + tbt[11], tbt[12], StandardCharsets.UTF_8)
                                    .equals("km"),
                    "guidance distance and unit");
            q.sync.navi("test", "GUIDING", 3, 1255, "Арбат", null);
            check(q.take().isEmpty(), "same rounded guidance is not repeated");
            q.sync.navi("test", "GUIDING", 3, 400, "Арбат", null);
            List<byte[]> near = q.take();
            check(near.size() == 1 && near.get(0)[12] == 1, "meters unit");
            q.sync.lanes("test", false, new int[0]);
            check(q.take().size() == 1, "first hide is sent");
            q.sync.lanes("test", false, new int[0]);
            check(q.take().isEmpty(), "repeated hide is not sent");
            Bundle bundle = new Bundle();
            bundle.put("1", 0x22);
            bundle.put("0", 0x11);
            q.sync.lanes("test", true, IpkInfoSync.laneIds(bundle));
            byte[] lanes = q.take().get(0);
            check(lanes[6] == 2 && lanes[8] == 0x11 && lanes[10] == 0x22, "lanes in key order");
        } finally {
            Locale.setDefault(previous);
        }
    }

    static void presentation() throws Exception {
        try (IpkLinkTest.Ivcd ivcd = new IpkLinkTest.Ivcd()) {
            IpkLink link = new IpkLink(ivcd.address(), 100, 400, 50, 200);
            IpkInfoSync sync = new IpkInfoSync(link);
            List<String> requests = Collections.synchronizedList(new ArrayList<>());
            sync.onPresentation((category, value) -> requests.add(category + "=" + value));
            link.start();

            while (ivcd.connections.isEmpty()) {
                Thread.sleep(10);
            }

            byte[] navi = IpkLinkTest.reply(0x80, 1);
            navi[2] = 9;
            navi[8] = 2;
            navi[9] = 1;
            ivcd.last().write(navi);
            ivcd.last().write(IpkLinkTest.reply(0x83, 2));
            ivcd.last().write(IpkLinkTest.reply(0x9e, 1));
            byte[] ignored = IpkLinkTest.reply(0x80, 1);
            ignored[7] = 1;
            ignored[8] = 4;
            ivcd.last().write(ignored);
            long deadline = System.nanoTime() + 5_000_000_000L;

            while (requests.size() < 4 && System.nanoTime() < deadline) {
                Thread.sleep(10);
            }

            check(
                    requests.equals(
                            List.of(
                                    "qg.car.cluster.NVS=1",
                                    "qg.car.cluster.FAVORITE=1",
                                    "qg.car.cluster.PHONE=2",
                                    "qg.car.cluster.CAMP=1")),
                    "presentation requests: " + requests);
            sync.readyToShow("test", "qg.car.cluster.NVS");
            sync.ready("test", "qg.car.cluster.MEDIA", 1);
            sync.presentation("test", "qg.car.cluster.REST", 2);
            sync.ready("test", "unknown", 1);

            while (ivcd.last().data().size() < 3 && System.nanoTime() < deadline) {
                Thread.sleep(10);
            }

            List<byte[]> data = ivcd.last().data();
            check(data.size() == 3, "readiness frames: " + data.size());
            check(data.get(0)[5] == 0x60 && data.get(0)[2] == 9 && data.get(0)[8] == 2, "show");
            check(data.get(1)[5] == 0x61 && data.get(1)[10] == 2, "media ready");
            check(data.get(2)[5] == 0x7b && data.get(2)[6] == 2, "rest request");
            link.stop();
        }
    }

    public static void main(String[] args) throws Exception {
        media();
        navigation();
        presentation();
        System.out.println("IpkInfoSyncTest PASS");
    }
}
