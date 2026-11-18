package com.qinggan.bluetooth;

import com.qinggan.bluetooth.profile.BTDevice;

import java.util.LinkedHashMap;
import java.util.Map;

/** Test double of the OEM device cache, keyed by the Bluetooth address. */
public final class BTDeviceManager implements BTConst {
    private final Map<String, BTDevice> devices = new LinkedHashMap<>();

    BTDeviceManager() {}

    public synchronized Map<String, BTDevice> getCachedDevices() {
        return devices;
    }

    public synchronized BTDevice findDevice(String address) {
        return devices.get(address);
    }

    public synchronized BTDevice addDevice(BTDevice device) {
        devices.put(device.getAddress(), device);

        return device;
    }

    public synchronized BTDevice findOrAddDevice(BTDevice device) {
        BTDevice cached = devices.get(device.getAddress());

        return cached != null ? cached : addDevice(device);
    }

    public synchronized void removeDevice(String address) {
        devices.remove(address);
    }

    public synchronized void removeAll() {
        devices.clear();
    }
}
