package com.qinggan.canbus.service;

import android.os.Bundle;

import java.text.DecimalFormat;
import java.text.DecimalFormatSymbols;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Head-unit information for the cluster, shared by the A9 CanBus entry and the A11 cluster service
 * entry: it keeps the state of the OEM InstrumentInteractionComponent and
 * VoyahInstrumentClusterInfoSync, encodes the changes with {@link IpkFrames} and writes them over
 * the one {@link IpkLink}. Cluster presentation requests reach the registered listener.
 */
final class IpkInfoSync {
    /** Presentation request of the cluster for one category, 1 to open and 2 to close. */
    interface PresentationListener {
        void presentation(String category, int value);
    }

    private final IpkLink link;
    private volatile PresentationListener presentation;
    private int sequence;
    // 0x6e source and device bytes persist between updates as in the OEM frame buffer.
    private int mediaSource, mediaDevice = 1;
    private String guide = "", icon, distance, unit, turnRoad, currentRoad;
    private int roadShown = -1, lanesShown = -1;

    IpkInfoSync(IpkLink link) {
        this.link = link;
        link.listen(this::received);
    }

    IpkLink link() {
        return link;
    }

    void onPresentation(PresentationListener value) {
        presentation = value;
    }

    private long send(String kind, String slot, String origin, byte[][] frames) {
        return send(kind, slot, origin, frames, null);
    }

    private long send(
            String kind, String slot, String origin, byte[][] frames, IpkLink.Receipt receipt) {
        return link.send(kind, slot, origin, frames, receipt);
    }

    private long send(String kind, String slot, String origin, byte[] frame) {
        return frame == null ? 0 : send(kind, slot, origin, new byte[][] {frame});
    }

    // ------------------------------------------------------------------
    // Phone 0x6a
    // ------------------------------------------------------------------

    long phone(
            String origin,
            String state,
            int vehicleCall,
            int duration,
            String name,
            String number,
            IpkLink.Receipt receipt) {
        return send(
                "phone",
                "phone",
                origin,
                IpkFrames.phone(state, vehicleCall, duration, name, number),
                receipt);
    }

    // ------------------------------------------------------------------
    // Media 0x6e and radio 0x6f
    // ------------------------------------------------------------------

    private static boolean playing(String state, boolean stop) {
        return "PLAY".equals(state) || "PAUSE".equals(state) || (stop && "STOP".equals(state));
    }

    /** Content byte [8] of 0x6e: 3 with a song, 1 without, 2 for the Bluetooth placeholder. */
    private static int content(String source, String song) {
        if (source.endsWith("_DISMISS") || song == null) {
            return 1;
        }

        if ("A2DP".equals(source)) {
            if ("蓝牙音乐".equals(song) || "Bluetooth music".equals(song)) {
                return 2;
            }

            if ("暂无歌曲".equals(song) || "no song".equals(song)) {
                return 1;
            }
        }

        return 3;
    }

    /** Search byte [7] of 0x6f from the OEM radio status strings. */
    private static int search(String song) {
        if ("正在搜索电台".equals(song) || "Searching radio station...".equals(song)) {
            return 1;
        }

        return "未搜索到有效电台".equals(song) || "No radio station available now".equals(song) ? 2 : 3;
    }

    /** Updates the persistent 0x6e source [6] and device [7] bytes as the OEM component does. */
    private void device(String source) {
        int[] values;

        switch (source) {
            case "SD":
                values = new int[] {1, 0};
                break;
            case "USB":
                values = new int[] {2, 2};
                break;
            case "A2DP":
                values = new int[] {3, 6};
                break;
            case "NetworkMusic":
                values = new int[] {4, mediaDevice};
                break;
            case "USB2":
                values = new int[] {5, 2};
                break;
            case "USB1_DISMISS":
                values = new int[] {mediaSource, 1};
                break;
            case "USB2_DISMISS":
                values = new int[] {mediaSource, 3};
                break;
            case "A2DP_DISMISS":
                values = new int[] {mediaSource, 5};
                break;
            default:
                return;
        }

        mediaSource = values[0];
        mediaDevice = values[1];
    }

    /**
     * Applies one MediaSrcInfo: as in the OEM component, the latest source replaces the others and
     * is sent while it plays or pauses (Bluetooth and radio also while stopped).
     */
    synchronized long media(
            String origin,
            String source,
            String state,
            float frequency,
            String song,
            String artist,
            String album) {
        if (source == null) {
            return 0;
        }

        device(source);

        switch (source) {
            case "SD":
            case "USB":
            case "USB2":
            case "USB1_DISMISS":
            case "USB2_DISMISS":
            case "NetworkMusic":
            case "A2DP":
            case "A2DP_DISMISS":
                return playing(state, source.startsWith("A2DP"))
                        ? send(
                                "media",
                                "media",
                                origin,
                                IpkFrames.media(
                                        mediaSource,
                                        mediaDevice,
                                        content(source, song),
                                        song,
                                        artist))
                        : 0;
            case "FM":
            case "AM":
                return playing(state, true)
                        ? send(
                                "radio",
                                "media",
                                origin,
                                IpkFrames.radio(
                                        "AM".equals(source) ? 2 : 1,
                                        search(song),
                                        2,
                                        Float.toString(frequency),
                                        null))
                        : 0;
            case "NetworkRadio":
            case "XMLY":
                return playing(state, false) ? network(origin, source, song, album) : 0;
            default:
                return 0;
        }
    }

    private long network(String origin, String source, String song, String album) {
        String unknown = "未知";

        return send(
                "radio",
                "media",
                origin,
                IpkFrames.radio(
                        "XMLY".equals(source) ? 4 : 3,
                        3,
                        5,
                        song == null || song.isEmpty() ? unknown : song,
                        album == null || album.isEmpty() ? unknown : album));
    }

    // ------------------------------------------------------------------
    // Turn by turn 0x68, current road 0x77, lanes 0x76
    // ------------------------------------------------------------------

    /** OEM GuidanceFormat.formatRemainDist: 50 m steps above 500 m, else rounded 10 m steps. */
    static int remain(int meters) {
        int rounded = meters > 500 ? meters - meters % 50 : (meters + 5) - (meters + 5) % 10;

        return rounded < 10 ? 0 : rounded;
    }

    static String distance(int meters) {
        int value = remain(meters);

        if (value >= 100000) {
            return String.valueOf(value / 1000);
        }

        if (value >= 1000) {
            return new DecimalFormat("0.#", DecimalFormatSymbols.getInstance(Locale.ROOT))
                    .format(value / 1000.0);
        }

        return String.valueOf(value);
    }

    /** The OEM unit resource: English strings for an English locale, Chinese otherwise. */
    static String unit(int meters, Locale locale) {
        boolean english = "en".equals(locale.getLanguage());

        return meters >= 1000 ? (english ? "km" : "公里") : (english ? "m" : "米");
    }

    /** Applies one NaviInfo as the OEM component does for roaming and guidance. */
    synchronized void navi(
            String origin,
            String state,
            int iconId,
            int nextDistance,
            String turnRoadName,
            String currentRoadName) {
        if ("ROAM".equals(state)) {
            roam(origin, currentRoadName);
        } else {
            guide = state == null ? "" : state;
            guidance(origin, iconId, nextDistance, turnRoadName);
        }
    }

    private void roam(String origin, String currentRoadName) {
        if (!"ROAM".equals(guide)) {
            guide = "ROAM";
            send("tbt", "navi", origin, IpkFrames.guideOff());
        }

        if (currentRoadName != null && (roadShown == -1 || !currentRoadName.equals(currentRoad))) {
            roadShown = 1;
            currentRoad = currentRoadName;
            send("road", "navi", origin, IpkFrames.road(currentRoadName));
        }
    }

    private void guidance(String origin, int iconId, int nextDistance, String turnRoadName) {
        String newIcon = String.valueOf(iconId);
        String newDistance = distance(nextDistance);
        String newUnit = unit(nextDistance, Locale.getDefault());
        boolean changed =
                !newIcon.equals(icon)
                        || !newDistance.equals(distance)
                        || !newUnit.equals(unit)
                        || (turnRoadName != null && !turnRoadName.equals(turnRoad));

        icon = newIcon;
        distance = newDistance;
        unit = newUnit;

        if (turnRoadName != null) {
            turnRoad = turnRoadName;
        }

        if (roadShown != 2) {
            roadShown = 2;
            currentRoad = "";
            send("road", "navi", origin, IpkFrames.roadOff());
            changed = true;
        }

        if (changed && turnRoad != null) {
            send("tbt", "navi", origin, IpkFrames.guide(iconId, distance, unit, turnRoad));
        }
    }

    /** Integer values of the lane bundle in the key order of its ArrayMap, by key hash. */
    static int[] laneIds(Bundle info) {
        if (info == null) {
            return new int[0];
        }

        List<String> keys = new ArrayList<>(info.keySet());
        keys.sort((left, right) ->
                left.hashCode() != right.hashCode()
                        ? Integer.compare(left.hashCode(), right.hashCode())
                        : left.compareTo(right));
        List<Integer> ids = new ArrayList<>();

        for (String key : keys) {
            if (info.get(key) instanceof Integer) {
                ids.add((Integer) info.get(key));
            }
        }

        int[] result = new int[ids.size()];

        for (int i = 0; i < result.length; i++) {
            result[i] = ids.get(i);
        }

        return result;
    }

    synchronized void lanes(String origin, boolean show, int[] ids) {
        if (!show) {
            if (lanesShown != 0) {
                lanesShown = 0;
                send("lanes", null, origin, IpkFrames.lanes(new int[0]));
            }

            return;
        }

        lanesShown = 1;
        send("lanes", null, origin, IpkFrames.lanes(ids));
    }

    // ------------------------------------------------------------------
    // Presentation readiness, A11 qg.cluster.service
    // ------------------------------------------------------------------

    void ready(String origin, String category, int state) {
        send("ready", null, origin, IpkFrames.ready(category, state));
    }

    void presentation(String origin, String category, int value) {
        send("presentation", null, origin, IpkFrames.presentation(category, value));
    }

    void readyToShow(String origin, String category) {
        int current;

        synchronized (this) {
            current = sequence;
        }

        send("ready_to_show", null, origin, IpkFrames.readyToShow(category, current));
    }

    // ------------------------------------------------------------------
    // Cluster cards 0x75
    // ------------------------------------------------------------------

    /** The cluster cards the Settings display page selects, in the order of its menu. */
    long cards(String origin, int[] values) {
        return send("cards", null, origin, IpkFrames.cards(values));
    }

    /** A complete IVC frame the A9 OEM component API passes to sendSocketCtl. */
    long raw(String origin, byte[] frame) {
        return send("raw", null, origin, new byte[][] {frame.clone()});
    }

    // ------------------------------------------------------------------
    // Cluster requests
    // ------------------------------------------------------------------

    private static String naviCategory(int view) {
        switch (view) {
            case 1:
                return "qg.car.cluster.NAVI_SMALL";
            case 2:
                return "qg.car.cluster.NVS";
            case 3:
                return "qg.car.cluster.NAVI_AR";
            case 4:
                return "qg.car.cluster.NAVI_FULL";
            default:
                return null;
        }
    }

    private static String cardCategory(int cmd) {
        switch (cmd) {
            case 0x81:
                return "qg.car.cluster.MEDIA_SMALL";
            case 0x83:
                return "qg.car.cluster.PHONE";
            case 0x9b:
                return "qg.car.cluster.REST";
            case 0x9e:
                return "qg.car.cluster.CAMP";
            default:
                return null;
        }
    }

    /** Maps the cluster requests of the OEM VoyahInstrumentCluster.handleSocketData. */
    private void received(byte[] frame) {
        if (frame.length < 10) {
            return;
        }

        PresentationListener current = presentation;
        int cmd = frame[5] & 255;

        if (cmd == 0x80) {
            navigationRequest(frame, current);

            return;
        }

        String category = cardCategory(cmd);

        if (current != null && category != null && (frame[6] == 1 || frame[6] == 2)) {
            current.presentation(category, frame[6]);
        }
    }

    /** 0x80: [6] switch of the navigation view [8], [7]=1 ignored, [9] favorite switch. */
    private void navigationRequest(byte[] frame, PresentationListener current) {
        if (frame[7] == 1) {
            return;
        }

        if (frame[6] != 0) {
            synchronized (this) {
                sequence = frame[2];
            }
        }

        if (current == null) {
            return;
        }

        String category = naviCategory(frame[8]);

        if (category != null && (frame[6] == 1 || frame[6] == 2)) {
            current.presentation(category, frame[6]);
        }

        if (frame[9] == 1 || frame[9] == 2) {
            current.presentation("qg.car.cluster.FAVORITE", frame[9]);
        }
    }
}
