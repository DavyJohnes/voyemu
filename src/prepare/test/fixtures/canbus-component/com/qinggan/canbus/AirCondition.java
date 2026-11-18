package com.qinggan.canbus;

import java.util.HashMap;
import java.util.Map;

/** Records the values the component sets, by setter name without "set". */
public class AirCondition {
    public final Map<String, Number> values = new HashMap<>();

    public void setAcAirVentCoolSts(int value) {
        values.put("AcAirVentCoolSts", value);
    }

    public void setAc_power_switch(int value) {
        // checkstyle-allow MethodName: OEM donor setter name
        values.put("Ac_power_switch", value);
    }

    public void setAirACStatus(int value) {
        values.put("AirACStatus", value);
    }

    public void setAirCirculationMode(int value) {
        values.put("AirCirculationMode", value);
    }

    public void setAirCirculationStatus(int value) {
        values.put("AirCirculationStatus", value);
    }

    public void setAirDUALAllStatus(int value) {
        values.put("AirDUALAllStatus", value);
    }

    public void setAirDUALStatus(int value) {
        values.put("AirDUALStatus", value);
    }

    public void setAirDisplaySW(int value) {
        values.put("AirDisplaySW", value);
    }

    public void setAirFrontSWStatus(int value) {
        values.put("AirFrontSWStatus", value);
    }

    public void setAirFrontWindowDefogger(int value) {
        values.put("AirFrontWindowDefogger", value);
    }

    public void setAirLeftTemperature(float value) {
        values.put("AirLeftTemperature", value);
    }

    public void setAirMode(int value) {
        values.put("AirMode", value);
    }

    public void setAirRearTemperature(float value) {
        values.put("AirRearTemperature", value);
    }

    public void setAirRearWindowDefogger(int value) {
        values.put("AirRearWindowDefogger", value);
    }

    public void setAirRightTemperature(float value) {
        values.put("AirRightTemperature", value);
    }

    public void setAirSWStatus(int value) {
        values.put("AirSWStatus", value);
    }

    public void setAirSupplyStatus(int value) {
        values.put("AirSupplyStatus", value);
    }

    public void setAirWindSpeed(int value) {
        values.put("AirWindSpeed", value);
    }

    public void setIGN_ON(int value) {
        // checkstyle-allow MethodName: OEM donor setter name
        values.put("IGN_ON", value);
    }

    public void setairworking_mode(int value) {
        // checkstyle-allow MethodName: OEM donor setter name
        values.put("airworking_mode", value);
    }
}
