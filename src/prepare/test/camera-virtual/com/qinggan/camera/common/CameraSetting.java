package com.qinggan.camera.common;

public class CameraSetting {
    static final CameraSetting INSTANCE = new CameraSetting();
    public int type;

    public static CameraSetting getInstance() {
        return INSTANCE;
    }

    public int getCameraType() {
        return type;
    }
}
