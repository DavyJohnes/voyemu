package com.qinggan.canbus.service;

/** NXP SDK Binder: climate is implemented; unimplemented vehicle domains stay UNKNOWN. */
final class ClimateService extends com.qinggan.canbus.ICanBusService.Stub {
    private final ClimateModel model;
    private final android.os.Handler handler =
            new android.os.Handler(android.os.Looper.getMainLooper());
    private final ClimateCallbacks<com.qinggan.canbus.AirCondition> delivery =
            new ClimateCallbacks<>(action -> handler.post(action));
    private final android.os.RemoteCallbackList<com.qinggan.canbus.ICanBusServiceCallback>
            callbacks = new android.os.RemoteCallbackList<>();

    ClimateService(android.content.Context context) {
        final android.content.SharedPreferences prefs =
                context.getSharedPreferences("voyemu-nxp-climate", 0);

        model =
                new ClimateModel(
                        new ClimateModel.Store() {
                            public java.util.Map<String, Integer> load() {
                                java.util.Map<String, Integer> values = new java.util.HashMap<>();

                                for (java.util.Map.Entry<String, ?> e : prefs.getAll().entrySet()) {
                                    if (e.getValue() instanceof Integer) {
                                        values.put(e.getKey(), (Integer) e.getValue());
                                    }
                                }

                                return values;
                            }

                            public boolean save(java.util.Map<String, Integer> values) {
                                android.content.SharedPreferences.Editor edit = prefs.edit();

                                for (java.util.Map.Entry<String, Integer> e : values.entrySet()) {
                                    edit.putInt(e.getKey(), e.getValue());
                                }

                                return edit.commit();
                            }
                        });
    }

    private int apply(android.os.Bundle bundle) {
        if (bundle == null) {
            return -1;
        }

        java.util.Map<String, Integer> patch = new java.util.HashMap<>();

        for (String key : bundle.keySet()) {
            com.qinggan.canbus.AirConditionState.valueOf(key);
            Object value = bundle.get(key);

            if (!(value instanceof Integer)) {
                throw new IllegalArgumentException("Climate integer");
            }

            patch.put(key, (Integer) value);
        }

        if (!model.set(patch)) {
            return -1;
        }

        changed();

        return 0;
    }

    private void changed() {
        delivery.enqueue(
                getAirCondition(),
                snapshot -> {
                    int count = callbacks.beginBroadcast();

                    try {
                        for (int i = 0; i < count; i++) {
                            try {
                                callbacks.getBroadcastItem(i).onAirConditionChanged(snapshot);
                            } catch (android.os.RemoteException ignored) {
                            }
                        }
                    } finally {
                        callbacks.finishBroadcast();
                    }
                });
    }

    @Override
    public synchronized boolean addCallback(com.qinggan.canbus.ICanBusServiceCallback p0) {
        if (p0 == null) {
            return false;
        }

        boolean added = callbacks.register(p0);

        if (added) {
            delivery.enqueue(
                    getAirCondition(),
                    snapshot -> {
                        try {
                            p0.onAirConditionChanged(snapshot);
                        } catch (android.os.RemoteException ignored) {
                        }
                    });
        }

        return added;
    }

    // Ignition ACC=2 and key=1 are intentional emulator availability, not measured vehicle
    // telemetry.
    @Override
    public synchronized int getAccStatus() {
        return 2;
    }

    @Override
    public synchronized com.qinggan.canbus.AirCondition getAirCondition() {
        com.qinggan.canbus.AirCondition air = new com.qinggan.canbus.AirCondition();

        air.setIGN_ON(1);
        int power = model.get("AC_POWER_SWITCH");
        air.setAirSWStatus(power);
        air.setAc_power_switch(power);
        air.setAirFrontSWStatus(power);
        air.setAirDisplaySW(1);
        air.setAirLeftTemperature(model.get("AC_LEFT_TEMP") / 10f);
        air.setAirRightTemperature(model.get("AC_RIGHT_TEMP") / 10f);
        air.setAirRearTemperature(model.get("AC_REAR_TEMP") / 10f);
        air.setairworking_mode(model.get("AC_WORKING_MODE"));
        air.setAcAirVentCoolSts(model.get("VENT_COOL_SET"));
        air.setAirDUALStatus(model.get("AC_DUAL"));
        air.setAirDUALAllStatus(model.get("AC_DUAL_ALL"));
        air.setAirACStatus(model.get("AC_SWITCH"));
        air.setAirMode(model.get("AC_AUTO"));
        air.setAirCirculationStatus(model.get("AC_RECIRC_AIR"));
        air.setAirCirculationMode(model.get("AC_RECIRC_AIR"));
        air.setAirFrontWindowDefogger(model.get("AC_FRONT_DEFROST_SWITCH"));
        air.setAirRearWindowDefogger(model.get("AC_REAR_DEFROST_SWITCH"));
        air.setAirSupplyStatus(model.get("AC_FLOW_MODE"));
        air.setAirWindSpeed(Math.max(0, model.get("AC_BLOWER") - 1));

        return air;
    }

    @Override
    public synchronized int getAirConditionState(com.qinggan.canbus.AirConditionState p0) {
        return p0 == null ? -9999 : model.get(p0.name());
    }

    @Override
    public synchronized int getAlarmData() {
        return -9999;
    }

    @Override
    public synchronized int getAmbientTemperature() {
        return -9999;
    }

    @Override
    public synchronized float getBatteryRemainingCapacity() {
        return -9999f;
    }

    @Override
    public synchronized com.qinggan.canbus.BatteryState getBatteryState() {
        return null;
    }

    @Override
    public synchronized int getBrakeFluidStatus() {
        return -9999;
    }

    @Override
    public synchronized com.qinggan.canbus.BrakePadStatus getBrakePadStatus() {
        return null;
    }

    @Override
    public synchronized com.qinggan.canbus.CameraState getCameraState() {
        return null;
    }

    @Override
    public synchronized java.lang.String getCanBoxVersion() {
        return null;
    }

    @Override
    public synchronized int[] getCanRawData(int p0) {
        return null;
    }

    @Override
    public synchronized int getComprehensiveFuelConsumption() {
        return -9999;
    }

    @Override
    public synchronized float getComprehensiveFuelConsumptionEX() {
        return -9999f;
    }

    @Override
    public synchronized com.qinggan.canbus.DVRState getDVRState() {
        return null;
    }

    @Override
    public synchronized com.qinggan.canbus.DoorStatus getDoorStatus() {
        return null;
    }

    @Override
    public synchronized int getEngineFluidStatus() {
        return -9999;
    }

    @Override
    public synchronized int getEngineSpeed() {
        return -9999;
    }

    @Override
    public synchronized int getEngineStatus() {
        return -9999;
    }

    @Override
    public synchronized int getEngineTemperature() {
        return -9999;
    }

    @Override
    public synchronized com.qinggan.canbus.RadarData getFrontRadarData() {
        return null;
    }

    @Override
    public synchronized com.qinggan.canbus.FuelLevel getFuelLevel() {
        return null;
    }

    @Override
    public synchronized com.qinggan.canbus.GearState getGearStatus() {
        return com.qinggan.canbus.GearState.Unknown;
    }

    @Override
    public synchronized int getHandBrakeStatus() {
        return -9999;
    }

    @Override
    public synchronized int getHevSysMode() {
        return -9999;
    }

    @Override
    public synchronized int getIllumination() {
        return -9999;
    }

    @Override
    public synchronized int getInstantFuelConsumption() {
        return -9999;
    }

    @Override
    public synchronized com.qinggan.ipksync.IIpkSyncService getIpkSyncSvr() {
        return null;
    }

    @Override
    public synchronized com.qinggan.canbus.RadarData getLeftRadarData() {
        return null;
    }

    @Override
    public synchronized com.qinggan.canbus.LightStatus getLightStatus() {
        return null;
    }

    @Override
    public synchronized com.qinggan.canbus.Odometer getOdometer() {
        return null;
    }

    @Override
    public synchronized com.qinggan.canbus.PowerLevel getPowerlLevel() {
        return null;
    }

    @Override
    public synchronized int getRainfallLevel() {
        return -9999;
    }

    @Override
    public synchronized int getReTracking() {
        return -9999;
    }

    @Override
    public synchronized com.qinggan.canbus.RealityWarningInfo getRealityWarningInfo() {
        return null;
    }

    @Override
    public synchronized com.qinggan.canbus.RadarData getRearRadarData() {
        return null;
    }

    @Override
    public synchronized com.qinggan.canbus.RadarData getRightRadarData() {
        return null;
    }

    @Override
    public synchronized com.qinggan.canbus.SWCAngle getSWCAngle() {
        return null;
    }

    @Override
    public synchronized com.qinggan.canbus.SeatBelt getSeatBeltStatus() {
        return null;
    }

    @Override
    public synchronized long getSecondaryOdometer() {
        return -9999L;
    }

    @Override
    public synchronized com.qinggan.canbus.TPMSInfo getTPMSInfo() {
        return null;
    }

    @Override
    public synchronized int getVehicleCenterControlEnabled(
            com.qinggan.canbus.VehicleCenterControlEnabled p0) {
        return -9999;
    }

    @Override
    public synchronized com.qinggan.canbus.VehicleIO getVehicleIO() {
        return null;
    }

    @Override
    public synchronized int getVehicleKeyState() {
        return 1;
    }

    @Override
    public synchronized int getVehicleSceneMode() {
        return -9999;
    }

    @Override
    public synchronized int getVehicleSpeed() {
        return -9999;
    }

    @Override
    public synchronized int getVehicleState(com.qinggan.canbus.VehicleState p0) {
        return -9999;
    }

    @Override
    public synchronized com.qinggan.canbus.WheelCount getWheelCount() {
        return null;
    }

    @Override
    public synchronized com.qinggan.canbus.WheelSpeed getWheelSpeed() {
        return null;
    }

    @Override
    public synchronized com.qinggan.canbus.WindowStatus getWindowStatus() {
        // The NXP launcher dereferences this snapshot; the SDK constructor defaults to CLOSED, not
        // UNKNOWN.
        com.qinggan.canbus.WindowStatus status = new com.qinggan.canbus.WindowStatus();

        status.fLWindow =
                status.fRWindow =
                        status.rLWindow = status.rRWindow = com.qinggan.canbus.WindowStatus.UNKNOWN;
        status.fLWindowHotProtect =
                status.fRWindowHotProtect =
                        status.rLWindowHotProtect =
                                status.rRWindowHotProtect = com.qinggan.canbus.WindowStatus.UNKNOWN;
        status.fLWindowLearn =
                status.fRWindowLearn =
                        status.rLWindowLearn =
                                status.rRWindowLearn = com.qinggan.canbus.WindowStatus.UNKNOWN;
        status.fLWindowPosition =
                status.fRWindowPosition =
                        status.rLWindowPosition =
                                status.rRWindowPosition = com.qinggan.canbus.WindowStatus.UNKNOWN;
        status.roll =
                status.rollPosition =
                        status.sunroof =
                                status.sunroofPosition =
                                        status.sunroofTilt =
                                                com.qinggan.canbus.WindowStatus.UNKNOWN;

        return status;
    }

    @Override
    public synchronized int getWiperFluidStatus() {
        return -9999;
    }

    @Override
    public synchronized boolean isSupportFunction(com.qinggan.canbus.FunctionType p0) {
        return false;
    }

    @Override
    public synchronized void queryVehicleState() {}

    @Override
    public synchronized boolean removeCallback(com.qinggan.canbus.ICanBusServiceCallback p0) {
        return p0 != null && callbacks.unregister(p0);
    }

    @Override
    public synchronized int requestTravellingInfo(com.qinggan.canbus.TravellingInfoType p0) {
        return -9999;
    }

    @Override
    public synchronized int requestVehicleCenterControlEnabledInfo() {
        return -9999;
    }

    @Override
    public synchronized int setAirConditionBundleState(android.os.Bundle p0) {
        return apply(p0);
    }

    @Override
    public synchronized void setAirConditionState(com.qinggan.canbus.AirConditionState p0, int p1) {
        if (p0 == null) {
            throw new IllegalArgumentException("Climate state");
        }

        java.util.Map<String, Integer> patch = new java.util.HashMap<>();
        String key = p0.name();

        if (p1 == 1
                && java.util.Arrays.asList(
                                "AC_POWER_SWITCH",
                                "AC_DUAL",
                                "AC_DUAL_ALL",
                                "AC_SWITCH",
                                "AC_AUTO",
                                "AC_RECIRC_AIR",
                                "AC_FRONT_DEFROST_SWITCH",
                                "VENT_COOL_SET")
                        .contains(key)) {
            p1 = model.get(key) == 0 ? 1 : 0;
        }

        patch.put(key, p1);

        if (!model.set(patch)) {
            throw new IllegalStateException("Climate persistence");
        }

        changed();
    }

    @Override
    public synchronized void setCanRawData(int p0, int[] p1) {}

    @Override
    public synchronized void setFactoryModeResult(int p0, java.lang.String p1) {}

    @Override
    public synchronized void setFactoryModeState(
            com.qinggan.canbus.VehicleStateAutoTest p0, java.lang.String p1, int p2) {}

    @Override
    public synchronized void setGearBoxType(com.qinggan.canbus.GearBoxType p0) {}

    @Override
    public synchronized int setVehicleAndAirConditionBundleState(
            android.os.Bundle p0, android.os.Bundle p1) {
        if (p0 != null && !p0.isEmpty()) {
            return -1;
        }

        return apply(p1);
    }

    @Override
    public synchronized int setVehicleBundleState(android.os.Bundle p0) {
        return -9999;
    }

    @Override
    public synchronized boolean setVehicleCenterControlEnabled(
            com.qinggan.canbus.VehicleCenterControlEnabled p0, int p1) {
        return false;
    }

    @Override
    public synchronized int setVehicleSceneMode(int p0) {
        return -9999;
    }

    @Override
    public synchronized void setVehicleState(com.qinggan.canbus.VehicleState p0, int p1) {}

    @Override
    public synchronized void updateCallListInfo(android.os.Bundle p0) {}

    @Override
    public synchronized void updateMediaPlayInfo(com.qinggan.canbus.MediaPlayInfo p0) {}

    @Override
    public synchronized void updateMediaPlayMode(com.qinggan.canbus.MediaPlayMode p0) {}

    @Override
    public synchronized void updateMediaPlayProgressInfo(
            com.qinggan.canbus.MediaPlayProgressInfo p0) {}

    @Override
    public synchronized void updateMediaSeqType(int p0) {}

    @Override
    public synchronized void updateMediaSrcInfo(com.qinggan.canbus.MediaSrcInfo p0) {}

    @Override
    public synchronized void updateMediaVolume(int p0) {}

    @Override
    public synchronized void updateNaviInfo(com.qinggan.canbus.NaviInfo p0) {}

    @Override
    public synchronized void updateNaviLaneInfo(boolean p0, android.os.Bundle p1) {}

    @Override
    public synchronized void updatePhoneInfo(com.qinggan.canbus.PhoneInfo p0) {}
}
