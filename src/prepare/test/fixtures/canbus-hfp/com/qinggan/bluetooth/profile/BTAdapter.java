package com.qinggan.bluetooth.profile;

public class BTAdapter {
    public final Hfp hfp = new Hfp();
    public android.os.Bundle data;
    public boolean emitDuringRegister, throwSnapshot;

    // Calls the Object methods of the callback proxy as the OEM adapter does.
    @SuppressWarnings({"ReturnValueIgnored", "SelfEquals"})
    public Hfp getHfp(IHfpCallback c) {
        hfp.callback = c;
        hfp.registrations++;
        c.hashCode();
        c.toString();

        if (!c.equals(c)) {
            throw new AssertionError();
        }

        if (emitDuringRegister) {
            c.on3WayCallStateChanged(130, 1, "+70000000001");
        }

        return hfp;
    }

    public android.os.Bundle phoneInfoSnapshot() {
        if (throwSnapshot) {
            throw new IllegalStateException("not included in diagnostics");
        }

        return data;
    }
}
