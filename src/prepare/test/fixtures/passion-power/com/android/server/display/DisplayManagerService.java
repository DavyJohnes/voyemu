package com.android.server.display;

import android.view.SurfaceControl;

/** Exact reflective ABI consumed by the real guest backend. */
public final class DisplayManagerService {
    public final Object mSyncRoot = new Object();
    public final Object mTempDisplayStateWorkQueue = new Object();
    public final Displays mLogicalDisplays = new Displays();
    public float mGlobalDisplayBrightness = 0.5f;
    public int updates;

    public void updateLogicalDisplaysLocked() {
        if (!Thread.holdsLock(mSyncRoot) || !Thread.holdsLock(mTempDisplayStateWorkQueue)) {
            throw new AssertionError("work queue then DMS lock required");
        }

        updates++;
    }

    public void replacePrimary() {
        mLogicalDisplays.primary.device = new LocalDisplayAdapter.LocalDisplayDevice();
    }

    public static final class Displays {
        public final LogicalDisplay primary =
                new LogicalDisplay(new LocalDisplayAdapter.LocalDisplayDevice());
        public final LogicalDisplay secondary =
                new LogicalDisplay(new VirtualDisplayAdapter.VirtualDisplayDevice());

        public LogicalDisplay get(int id) {
            return id == 0 ? primary : id == 1 ? secondary : null;
        }
    }

    public static final class LogicalDisplay {
        DisplayDevice device;

        LogicalDisplay(DisplayDevice device) {
            this.device = device;
        }

        public DisplayDevice getPrimaryDisplayDeviceLocked() {
            return device;
        }

        public void configureDisplayLocked(
                SurfaceControl.Transaction transaction, DisplayDevice actual, boolean blank) {
            if (actual != device || blank != (device.info.state == 1)) {
                throw new AssertionError("exact device and blank state required");
            }
        }
    }
}

class DisplayDevice {
    final Info info;

    DisplayDevice(String unique) {
        info = new Info(unique);
    }

    public Info getDisplayDeviceInfoLocked() {
        return info;
    }

    public Runnable requestDisplayStateLocked(int state, float brightness) {
        if (brightness != 0.5f) {
            throw new AssertionError("global brightness preserved");
        }

        return () -> info.state = state;
    }

    public void performTraversalLocked(SurfaceControl.Transaction transaction) {}

    static final class Info {
        final String uniqueId;
        int state = 2;

        Info(String unique) {
            uniqueId = unique;
        }
    }
}

class LocalDisplayAdapter {
    static final class LocalDisplayDevice extends DisplayDevice {
        LocalDisplayDevice() {
            super("local:8141241408256768");
        }
    }
}

class VirtualDisplayAdapter {
    static final class VirtualDisplayDevice extends DisplayDevice {
        VirtualDisplayDevice() {
            super("virtual:com.android.emulator.multidisplay:1234562");
        }

        public void setDisplayState(boolean on) {
            info.state = on ? 2 : 1;
        }
    }
}
