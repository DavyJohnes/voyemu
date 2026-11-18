package com.qinggan.canbus;

public class NaviInfo {
    public GuideState guide;
    public int icon, distance;
    public String turnRoad, currentRoad;

    public GuideState getGuideState() {
        return guide;
    }

    public int getTbtIconId() {
        return icon;
    }

    public int getNextDistance() {
        return distance;
    }

    public String getTurnRoadName() {
        return turnRoad;
    }

    public String getCurrentRoadName() {
        return currentRoad;
    }
}
