package com.qinggan.canbus.service;

import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import android.util.Log;

import com.qinggan.canbus.VehicleState;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

/**
 * QGAPI IInstrumentClusterManagerService Binder endpoint: the cluster theme, and the head-unit
 * information, presentation readiness and cluster cards of the OEM InstrumentClusterService written
 * over the IPK link.
 */
final class ClusterThemeBinder extends Binder {
    static final String DESCRIPTOR = "com.qinggan.cluster.IInstrumentClusterManagerService";
    static final String CALLBACK = "com.qinggan.cluster.IInstrumentClusterManagerCallback";
    private static final String CONCERN = "CONCERN_CLUSTER_STATE";
    private static final int MODE = 8, SKIN = 27;
    private final CanBusStubService canbus;
    private final Map<IBinder, IBinder.DeathRecipient> callbacks = new HashMap<>();
    private final Object publicationLock = new Object();

    // Ordinals of the com.qinggan.cluster.info enums in the OEM QGAPI.
    private static final String[] MEDIA_SOURCES = {
        "OFF",
        "FM",
        "AM",
        "Disc",
        "TV",
        "Navi",
        "Phone",
        "IPod",
        "AUX",
        "USB",
        "SD",
        "DVBT",
        "A2DP",
        "Other",
        "CDC",
        "Network",
        "QQ",
        "IPDALink",
        "NetworkMusic",
        "NetworkRadio",
        "USB2",
        "USB1_DISMISS",
        "USB2_DISMISS",
        "A2DP_DISMISS",
        "XMLY"
    };
    private static final String[] MEDIA_STATES = {"CLEAN", "PLAY", "PAUSE", "STOP"};
    private static final String[] PHONE_STATES = {
        "NOTHING", "COMING_CALL", "HOLD", "HANG_UP", "GOING_CALL", "CONNECTED"
    };
    private static final String[] GUIDE_STATES = {
        "START",
        "AUTO_STOP",
        "MAN_STOP",
        "YAW_GUIDE",
        "OVER_PASS",
        "GUIDING",
        "SIMULATING",
        "ROAM",
        "UNKNOWN"
    };

    ClusterThemeBinder(CanBusStubService canbus) {
        this.canbus = canbus;
        attachInterface(null, DESCRIPTOR);
        canbus.ipk().onPresentation(this::presentation);
    }

    /**
     * Reads a Parcelable enum written by writeParcelable: creator name, ordinal, optional value.
     */
    private static String parcelEnum(Parcel data, String[] names, boolean value) {
        if (data.readString() == null) {
            return null;
        }

        int ordinal = data.readInt();

        if (value) {
            data.readInt();
        }

        return ordinal >= 0 && ordinal < names.length ? names[ordinal] : null;
    }

    private static boolean present(Parcel data) {
        return data.readInt() != 0;
    }

    /** Transactions 3..5, 8..12 and 22 go to the cluster; other OEM operations are unsupported. */
    private void other(int code, Parcel data) {
        if ((code < 3 || code > 5) && (code < 8 || code > 12) && code != 22) {
            Log.w("CanBusStub", "unsupported cluster operation " + code);

            return;
        }

        try {
            info(code, data);
        } catch (IllegalStateException rejected) {
            Log.w("CanBusStub", "cluster information rejected: " + rejected.getMessage());
        }
    }

    private void info(int code, Parcel data) {
        IpkInfoSync ipk = canbus.ipk();

        switch (code) {
            case 3:
                {
                    String category = data.readString();
                    ipk.ready("cluster", category, data.readInt());
                    break;
                }
            case 4:
                {
                    String category = data.readString();
                    ipk.presentation("cluster", category, data.readInt());
                    break;
                }
            case 5:
                ipk.readyToShow("cluster", data.readString());
                break;
            case 8:
                if (present(data)) {
                    media(ipk, data);
                }

                break;
            case 9:
                if (present(data)) {
                    phone(ipk, data);
                }

                break;
            case 11:
                if (present(data)) {
                    navi(ipk, data);
                }

                break;
            case 12:
                {
                    boolean shown = data.readInt() != 0;
                    android.os.Bundle lanes = present(data) ? data.readBundle() : null;
                    ipk.lanes("cluster", shown, IpkInfoSync.laneIds(shown ? lanes : null));
                    break;
                }
            case 22:
                ipk.cards("cluster", ClusterCards.read(data));
                break;
            default:
                // 10 updateCallListInfo: the OEM cluster service sends no IVC frame for it.
                break;
        }
    }

    /** MediaSrcInfo: srcType, mediaType, frequency, mediaState, songName, artist, albumName. */
    private static void media(IpkInfoSync ipk, Parcel data) {
        String source = parcelEnum(data, MEDIA_SOURCES, true);

        parcelEnum(data, new String[0], true);
        float frequency = data.readFloat();
        String state = parcelEnum(data, MEDIA_STATES, true);
        String song = data.readString(), artist = data.readString();
        ipk.media("cluster", source, state, frequency, song, artist, data.readString());
    }

    /**
     * PhoneInfo: phoneNum, name, duration, phoneState, deviceName, connectedStatus, isVehicleCall.
     */
    private static void phone(IpkInfoSync ipk, Parcel data) {
        String number = data.readString(), name = data.readString();
        int duration = data.readInt();
        String state = parcelEnum(data, PHONE_STATES, true);

        data.readString();
        data.readInt();
        int vehicleCall = data.readInt();

        if (state != null) {
            ipk.phone("cluster", state, vehicleCall, duration, name, number, null);
        }
    }

    /** NaviInfo up to tbtIconId in the order of its writeToParcel. */
    private static void navi(IpkInfoSync ipk, Parcel data) {
        String guide = parcelEnum(data, GUIDE_STATES, false);

        parcelEnum(data, new String[0], false);
        parcelEnum(data, new String[0], true);

        for (int i = 0; i < 3; i++) {
            data.readInt();
        }

        int nextDistance = data.readInt();
        data.readInt();
        data.readInt();
        String currentRoad = data.readString(), turnRoad = data.readString();
        data.readInt();
        data.readString();
        data.readInt();
        data.readInt();
        data.readString();
        data.readInt();
        int icon = data.readInt();
        ipk.navi("cluster", guide, icon, nextDistance, turnRoad, currentRoad);
    }

    /** Forwards a cluster presentation request to the subscribers as setPresentationState. */
    private void presentation(String category, int value) {
        ArrayList<IBinder> snapshot;

        synchronized (callbacks) {
            snapshot = new ArrayList<>(callbacks.keySet());
        }

        for (IBinder callback : snapshot) {
            Parcel data = Parcel.obtain();

            try {
                data.writeInterfaceToken(CALLBACK);
                data.writeString("CONCERN_PRESENTATION");
                data.writeString(category);
                data.writeInt(value);
                callback.transact(3, data, null, IBinder.FLAG_ONEWAY);
            } catch (RemoteException e) {
                Log.w("CanBusStub", "cluster presentation callback failed", e);
            } finally {
                data.recycle();
            }
        }
    }

    private static int state(Parcel data) {
        if (data.readInt() != 1) {
            throw new IllegalArgumentException("cluster state required");
        }

        int ordinal = data.readInt(), value = data.readInt();

        if ((ordinal == MODE && value != 9) || (ordinal == SKIN && value != 28)) {
            throw new IllegalArgumentException("invalid cluster state");
        }

        return ordinal == MODE || ordinal == SKIN ? ordinal : -1;
    }

    @Override
    protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
            throws RemoteException {
        if (code == INTERFACE_TRANSACTION) {
            reply.writeString(DESCRIPTOR);

            return true;
        }

        if (code != 1 && code != 6 && code != 7 && code != 19 && code != 20) {
            if (code >= 1 && code <= 23) {
                data.enforceInterface(DESCRIPTOR);
                other(code, data);
                reply.writeNoException();

                return true;
            }

            return super.onTransact(code, data, reply, flags);
        }

        data.enforceInterface(DESCRIPTOR);

        switch (code) {
            case 1:
                reply.writeNoException();
                reply.writeInt(1);

                return true;
            case 6:
                {
                    int which = state(data), value = data.readInt();

                    if (which == -1) {
                        Log.w("CanBusStub", "unsupported cluster state write");
                        reply.writeNoException();

                        return true;
                    }

                    if (which == SKIN
                            ? !ClusterTheme.validSkin(value)
                            : !ClusterTheme.validMode(value)) {
                        throw new IllegalArgumentException("unsupported theme");
                    }

                    canbus.setVehicleState(
                            which == SKIN
                                    ? VehicleState.IPK_THEME_SKIN
                                    : VehicleState.IPK_THEM_MODE,
                            value);
                    reply.writeNoException();

                    return true;
                }
            case 7:
                {
                    int which = state(data);
                    // QGAPI returns -1 when the cluster state is unavailable.
                    int value =
                            which == -1
                                    ? -1
                                    : canbus.getVehicleState(
                                            which == SKIN
                                                    ? VehicleState.IPK_THEME_SKIN
                                                    : VehicleState.IPK_THEM_MODE);
                    reply.writeNoException();
                    reply.writeInt(value);

                    return true;
                }
            case 19:
                {
                    final IBinder callback = data.readStrongBinder();

                    if (callback != null) {
                        synchronized (callbacks) {
                            if (!callbacks.containsKey(callback)) {
                                IBinder.DeathRecipient death =
                                        () -> {
                                            synchronized (callbacks) {
                                                callbacks.remove(callback);
                                            }
                                        };
                                callback.linkToDeath(death, 0);
                                callbacks.put(callback, death);
                            }
                        }
                    }

                    reply.writeNoException();

                    if (callback != null) {
                        publish(callback);
                    }

                    return true;
                }
            default:
                {
                    IBinder callback = data.readStrongBinder();

                    synchronized (callbacks) {
                        IBinder.DeathRecipient death = callbacks.remove(callback);

                        if (death != null) {
                            callback.unlinkToDeath(death, 0);
                        }
                    }

                    reply.writeNoException();

                    return true;
                }
        }
    }

    void changed() {
        publish(null);
    }

    private void publish(IBinder initial) {
        synchronized (publicationLock) {
            int[] theme = canbus.clusterThemeSnapshot();
            ArrayList<IBinder> snapshot;

            synchronized (callbacks) {
                snapshot = new ArrayList<>();

                if (initial == null) {
                    snapshot.addAll(callbacks.keySet());
                } else if (callbacks.containsKey(initial)) {
                    snapshot.add(initial);
                }
            }

            for (IBinder callback : snapshot) {
                send(callback, MODE, theme[0]);
                send(callback, SKIN, theme[1]);
            }
        }
    }

    private void send(IBinder callback, int state, int value) {
        Parcel data = Parcel.obtain();

        try {
            data.writeInterfaceToken(CALLBACK);
            data.writeString(CONCERN);
            data.writeInt(1);
            data.writeInt(state);
            data.writeInt(state == MODE ? 9 : 28);
            data.writeInt(value);
            callback.transact(4, data, null, IBinder.FLAG_ONEWAY);
        } catch (RemoteException e) {
            Log.w("CanBusStub", "cluster callback failed", e);
        } finally {
            data.recycle();
        }
    }
}
