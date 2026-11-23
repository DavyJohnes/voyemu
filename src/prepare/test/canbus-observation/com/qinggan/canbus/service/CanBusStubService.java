package com.qinggan.canbus.service;

class CanBusStubService extends android.os.Binder {
    boolean accept;
    final java.util.List<Integer> power = new java.util.ArrayList<>();
    final java.util.List<Integer> hostPower = new java.util.ArrayList<>();

    CanBusStubService(Object c, BaseCanBusComponent component) {}

    org.json.JSONObject observe(CanObservationRequest r) {
        throw new AssertionError();
    }

    org.json.JSONObject phoneDiagnostics() {
        throw new AssertionError();
    }

    org.json.JSONObject ipkHistory(String r) {
        throw new AssertionError();
    }

    org.json.JSONObject modeHistory(String r) {
        throw new AssertionError();
    }

    void startIpk() {
        throw new AssertionError();
    }

    VehicleInputSnapshot currentInputs() {
        throw new AssertionError();
    }

    void setAccStatus(int s) {
        power.add(s);
    }

    void registrationObserved(boolean b) {
        throw new AssertionError();
    }

    void attachModel() {
        throw new AssertionError();
    }

    Object phoneDeliveryDiagnostics() {
        throw new AssertionError();
    }

    void stopModelPolling() {
        throw new AssertionError();
    }

    void registerClusterTheme() {
        throw new AssertionError();
    }

    boolean applyVehicleInputs(String s) {
        return accept;
    }

    boolean applyVehicleInputs(String s, int acc) {
        if (accept) {
            hostPower.add(acc);
        }

        return accept;
    }

    boolean applyVehicleSnapshot(long a, int b, int c, int d, int e, int f) {
        throw new AssertionError();
    }
}

class VehicleInputSnapshot {}

class BaseCanBusComponent {}

class HfpPhoneInfoBridge {
    HfpPhoneInfoBridge(Object a, Object b, Object c) {
        throw new AssertionError();
    }

    void start() {
        throw new AssertionError();
    }

    void close() {
        throw new AssertionError();
    }
}
