package com.qinggan.canbus;

/** The VehicleState constants the component uses; PW, FEV and DVR keep their OEM numbers. */
public enum VehicleState {
    HUM_VSP_FUNCTION_SW(665),
    IVI_SOC_MODESET(957),
    IPK_THEM_MODE(1),
    IPK_THEME_SKIN(2),
    TJA_HWA_STS(3),
    DRIVING_MODE_SET(4),
    IVI_AVM_NAVI_STATE(5),
    FRONT_SEAT_HEATING_COMMAND_LEFT(6),
    FRONT_SEAT_HEATING_COMMAND_RIGHT(7),
    FRONT_SEAT_HEATING_SWITCH_LEFT(8),
    FRONT_SEAT_HEATING_SWITCH_RIGHT(9),
    FRONT_SEAT_VENTILATION_COMMAND_LEFT(10),
    FRONT_SEAT_VENTILATION_COMMAND_RIGHT(11),
    FRONT_SEAT_VENTILATION_SWITCH_LEFT(12),
    FRONT_SEAT_VENTILATION_SWITCH_RIGHT(13),
    FRONT_SEAT_MASS_INTEN_LEFT(14),
    FRONT_SEAT_MASS_INTEN_RIGHT(15),
    FRONT_SEAT_MASS_SWITCH_LEFT(16),
    FRONT_SEAT_MASS_SWITCH_RIGHT(17),
    DVR_DISPLAY_REQ(752),
    DVR_TOUCH_MOTION(753),
    DVR_COORDINATE_X(754),
    DVR_COORDINATE_Y(755);
    private final int value;

    VehicleState(int value) {
        this.value = value;
    }

    public int getValue() {
        return value;
    }
}
