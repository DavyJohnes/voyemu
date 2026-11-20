package com.qinggan.bluetooth.profile;

public interface IBTProfile {
    boolean connect(BTDevice p0);

    boolean connect(String p0);

    boolean disconnect(BTDevice p0);

    boolean disconnect(String p0);

    int getConnectionState();

    int getConnectionState(BTDevice p0);

    @Deprecated
    boolean isConnected();

    boolean isProfileReady();
}
