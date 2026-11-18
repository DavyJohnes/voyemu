package com.qinggan.canbus;

public final class Odometer {
    public int mBeenTravelingMileage, mCanTravelMileage, mChargeReaminTime, mEVCruisingRange;
    public int mEnduranceMileage;
    public int mEVCruisingRange_wltc;
    // checkstyle-allow MemberName: OEM donor field name
    public int mCanTravelMileage_real;
    // checkstyle-allow MemberName: OEM donor field name
    public int mEVCruisingRang_real;
    // checkstyle-allow MemberName: OEM donor field name
    public float mPowerConsumption, totalTravelingMileage;

    public void setTotalTravelingMileage(float value) {
        totalTravelingMileage = value;
    }
}
