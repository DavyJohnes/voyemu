package com.qinggan.canbus;

public final class LightStatus {
    public int dayLight, dippedBeam, directionL, directionR, fogRear, mainBeam, positionLamp;
    public int headLightLevel = -1, headLightStatusOperability = -1;

    public void setHeadLightLevel(int value) {
        headLightLevel = value;
    }

    public void setHeadLightStatusOperability(int value) {
        headLightStatusOperability = value;
    }
}
