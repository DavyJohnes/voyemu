package com.qinggan.canbus.service;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Vehicle of the power mode handler that records what the handler reads and commands, in the token
 * form of DriveModeStartupOemTest; the maps hold its QGSettings, account, vehicle type, online
 * configuration and properties.
 */
public final class RecordingVehicle extends PowerModeVehicle {
    public final List<String> tokens = new ArrayList<>();
    public final Map<String, Integer> settings = new HashMap<>();
    public final Map<String, Integer> global = new HashMap<>();
    public final Map<String, Integer> states = new HashMap<>();
    public final Map<String, String> properties = new HashMap<>();
    private final Set<String> stateNames;
    public boolean guest = true;
    public int type = 134;
    public int matrix;

    public RecordingVehicle(Set<String> stateNames) {
        super(null);
        this.stateNames = stateNames;
    }

    @Override
    public int savedInt(String key, int value) {
        tokens.add("qg-get " + key);

        return settings.getOrDefault(key, value);
    }

    @Override
    public void saveInt(String key, int value) {
        tokens.add((stateNames.contains(key) ? "save " : "qg-put ") + key);
        settings.put(key, value);
    }

    @Override
    public int globalInt(String key, int value) {
        tokens.add("qg-global " + key);

        return global.getOrDefault(key, value);
    }

    @Override
    public boolean guestAccount() {
        tokens.add("account");

        return guest;
    }

    @Override
    public int vehicleTypeId() {
        tokens.add("vehicle-type");

        return type;
    }

    @Override
    public int onlineConfig(String item) {
        tokens.add("online " + item);

        return matrix;
    }

    @Override
    public String property(String key, String value) {
        tokens.add("prop " + key);

        return properties.getOrDefault(key, value);
    }

    @Override
    public int vehicleState(String state) {
        tokens.add("get " + state);

        return states.getOrDefault(state, 0);
    }

    @Override
    public int initVehicleLastStatus(String state, int value) {
        tokens.add("init " + state + "=" + value);
        int last = settings.getOrDefault(state, value);
        states.put(state, last);

        return last;
    }

    @Override
    public void cacheVehicleState(String state, int value) {
        tokens.add("cache " + state);
        states.put(state, value);
    }

    @Override
    public void setVehicleState(String state, int value) {
        tokens.add("set " + state + "=" + value);
        states.put(state, value);
    }

    @Override
    public void setVehicleBundle(Map<String, Integer> bundle) {
        for (Map.Entry<String, Integer> state : bundle.entrySet()) {
            tokens.add("put " + state.getKey() + "=" + state.getValue());
            states.put(state.getKey(), state.getValue());
        }

        tokens.add("bundle");
    }

    @Override
    public void publishVehicleState(String state, int value) {
        tokens.add("publish " + state + "=" + value);
    }

    @Override
    public void accStateChanged(int acc) {
        tokens.add("acc=" + acc);
    }
}
