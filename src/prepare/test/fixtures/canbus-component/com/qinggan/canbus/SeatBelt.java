package com.qinggan.canbus;

/** Shape checked against the pinned OEM Parcelable by CanbusSeatBeltTest. */
public class SeatBelt {
    public static final int SEATBELT_STATE_PULLED = 0;
    public static final int SEATBELT_STATE_PUSHED = 1;
    public static final int SEATBELT_STATE_UNSUPPORT = -1;
    public int driverSeatBeltState;
    public int passengerSeatBeltState;
    public int secondLeftSeatBeltState;
    public int secondMidSeatBeltState;
    public int secondRightSeatBeltState;
}
