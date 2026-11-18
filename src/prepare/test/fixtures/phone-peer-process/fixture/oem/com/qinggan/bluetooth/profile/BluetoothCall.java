package com.qinggan.bluetooth.profile;

/** Test-only recording of the exact original OEM constructor ABI; never part of the artifact. */
public final class BluetoothCall {
    public final BTDevice device;
    public final int id, state, previous;
    public final boolean multi, outgoing;
    public final String number;

    public BluetoothCall(
            BTDevice device,
            int id,
            boolean multi,
            String number,
            boolean outgoing,
            int state,
            int previous) {
        this.device = device;
        this.id = id;
        this.multi = multi;
        this.number = number;
        this.outgoing = outgoing;
        this.state = state;
        this.previous = previous;
    }
}
