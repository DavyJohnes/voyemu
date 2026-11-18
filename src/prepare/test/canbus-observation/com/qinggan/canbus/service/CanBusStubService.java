package com.qinggan.canbus.service;

class CanBusStubService extends android.os.Binder {
    CanBusStubService(Object c, BaseCanBusComponent component) {
        throw new AssertionError();
    }

    org.json.JSONObject observe(CanObservationRequest r) {
        throw new AssertionError();
    }

    org.json.JSONObject phoneDiagnostics() {
        throw new AssertionError();
    }

    org.json.JSONObject ipkHistory(String r) {
        throw new AssertionError();
    }

    void startIpk() {
        throw new AssertionError();
    }

    VehicleInputSnapshot currentInputs() {
        throw new AssertionError();
    }

    void setAccStatus(int s) {
        throw new AssertionError();
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
        throw new AssertionError();
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
