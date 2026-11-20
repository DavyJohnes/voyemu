package com.voyemu.output;

import android.app.Service;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;
import android.view.Display;

/**
 * Starts the OEM second-screen home (SecondMainActivity) on the secondary virtual display after
 * boot. Implements a state machine with bounded retry and postconditions.
 *
 * <p>States: WAIT_TARGET_DISPLAY → ENSURE_MAIN → ENSURE_SECOND → VERIFY → READY
 *
 * <p>Lab bootstrap helper — not an OEM application. The previous unconditional kill of the entire
 * launcher package has been removed. The secondary display is resolved by its stable name (the same
 * rule as WindowRouter in the launcher): the emulator recreates the virtual display on every
 * multidisplay-provider restart and its id keeps shifting (1→2→...→14→1), so a hardcoded display 1
 * makes this service wait out all retries or start the second home on the wrong display.
 */
public class SecondScreenService extends Service {
    private static final String TAG = "VoyemuBootstrap";

    static final int MAX_TRIES = 60;
    static final long RETRY_INTERVAL_MS = 1000;
    static final long VERIFY_DELAY_MS = 2000;

    private enum State {
        WAIT_TARGET_DISPLAY,
        ENSURE_MAIN,
        ENSURE_SECOND,
        VERIFY,
        READY
    }

    private final Handler handler = new Handler(Looper.getMainLooper());
    private State state = State.WAIT_TARGET_DISPLAY;
    private int tries = 0;
    private int generation = 0; // protects against duplicate/expired callbacks

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        ActivationPublisher.publish();

        // Idempotent: if already running, don't restart the state machine
        if (state == State.READY) {
            Log.i(TAG, "already READY, ignoring duplicate start");

            return START_NOT_STICKY;
        }

        generation++;
        advance(generation);

        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        generation++; // invalidate all pending callbacks
        handler.removeCallbacksAndMessages(null);
        super.onDestroy();
    }

    private void advance(final int gen) {
        if (gen != generation) {
            Log.d(TAG, "stale callback from generation " + gen + " (current " + generation + ")");

            return;
        }

        switch (state) {
            case WAIT_TARGET_DISPLAY:
                {
                    Display display = secondaryDisplay();

                    if (display != null) {
                        Log.i(TAG, "secondary display ready (id " + display.getDisplayId() + ")");
                        state = State.ENSURE_MAIN;
                        advance(gen);
                    } else {
                        tries++;

                        if (tries > MAX_TRIES) {
                            Log.e(
                                    TAG,
                                    "secondary display not available after "
                                            + MAX_TRIES
                                            + " tries — giving up");
                            stopSelf();

                            return;
                        }

                        Log.i(
                                TAG,
                                "waiting for the secondary display"
                                        + " (try "
                                        + tries
                                        + "/"
                                        + MAX_TRIES
                                        + ")");
                        handler.postDelayed(
                                new Runnable() {
                                    @Override
                                    public void run() {
                                        advance(gen);
                                    }
                                },
                                RETRY_INTERVAL_MS);
                    }

                    break;
                }

            case ENSURE_MAIN:
                // Main launcher is started by the system HOME resolver;
                // we do not kill or restart it here. The profile's configure
                // step pins HOME to MainActivity (set-home-activity), and the
                // build verifies the choice survives a reboot.
                Log.i(TAG, "ENSURE_MAIN: main launcher managed by system HOME");
                state = State.ENSURE_SECOND;
                advance(gen);
                break;

            case ENSURE_SECOND:
                {
                    // Re-resolve at start time: the id may have shifted since the
                    // display appeared.
                    Display display = secondaryDisplay();

                    if (display == null) {
                        retrySecondStart(gen, "secondary display gone");

                        return;
                    }

                    int targetDisplayId = display.getDisplayId();
                    Log.i(
                            TAG,
                            "ENSURE_SECOND: starting SecondMainActivity on display "
                                    + targetDisplayId);

                    try {
                        Intent i = new Intent("android.intent.action.MAIN");
                        i.setComponent(
                                new ComponentName(
                                        "com.qinggan.app.launcher",
                                        "com.qinggan.secondlauncher.activity.SecondMainActivity"));
                        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                        android.app.ActivityOptions o = android.app.ActivityOptions.makeBasic();
                        o.setLaunchDisplayId(targetDisplayId);
                        startActivity(i, o.toBundle());
                        Log.i(TAG, "SecondMainActivity start issued");
                    } catch (Exception e) {
                        // checkstyle-allow IllegalCatch: platform launch failure enters the bounded
                        // secondary-display retry
                        Log.w(TAG, "ENSURE_SECOND: start failed: " + e);
                        // Retry after delay, bounded like every other state.
                        retrySecondStart(gen, null);

                        return;
                    }

                    state = State.VERIFY;
                    handler.postDelayed(
                            new Runnable() {
                                @Override
                                public void run() {
                                    advance(gen);
                                }
                            },
                            VERIFY_DELAY_MS);
                    break;
                }

            case VERIFY:
                // Postcondition: the secondary display still exists and is not
                // the only display. A full window/task placement check requires
                // dumpsys which is not available from a plain service; we verify
                // display presence.
                if (secondaryDisplay() != null) {
                    Log.i(TAG, "VERIFY: secondary display confirmed present");
                    state = State.READY;
                    Log.i(TAG, "READY: bootstrap complete (tries=" + tries + ")");
                    stopSelf();
                } else {
                    Log.w(
                            TAG,
                            "VERIFY: the secondary display"
                                    + " disappeared — restarting state machine");
                    state = State.WAIT_TARGET_DISPLAY;
                    tries = 0;
                    advance(gen);
                }

                break;

            case READY:
                Log.i(TAG, "READY: already complete");
                stopSelf();
                break;
            default:
                break;
        }
    }

    /**
     * Bounded retry for ENSURE_SECOND: a permanently broken start must not spin the main looper
     * forever — the class doc promises bounded retry.
     */
    private void retrySecondStart(final int gen, String reason) {
        tries++;

        if (tries > MAX_TRIES) {
            Log.e(
                    TAG,
                    "SecondMainActivity not started after "
                            + MAX_TRIES
                            + " tries"
                            + (reason == null ? "" : " (" + reason + ")")
                            + " — giving up");
            stopSelf();

            return;
        }

        if (reason != null) {
            Log.i(TAG, "ENSURE_SECOND: " + reason + " (try " + tries + "/" + MAX_TRIES + ")");
        }

        handler.postDelayed(
                new Runnable() {
                    @Override
                    public void run() {
                        advance(gen);
                    }
                },
                RETRY_INTERVAL_MS);
    }

    /**
     * The secondary display's id: a non-default display whose name contains Emulator2D — the same
     * rule as WindowRouter in the launcher. -1 while the emulator has not created it yet.
     */
    private int resolveSecondaryDisplayId() {
        DisplayManager dm = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);

        if (dm == null) {
            return -1;
        }

        Display[] displays = dm.getDisplays();

        if (displays == null || displays.length > 64) {
            return -1;
        }

        int[] ids = new int[displays.length],
                widths = new int[displays.length],
                heights = new int[displays.length];
        String[] names = new String[displays.length];

        for (int i = 0; i < displays.length; i++) {
            Display display = displays[i];
            ids[i] = display.getDisplayId();
            names[i] = display.getName();
            Display.Mode mode = display.getMode();
            widths[i] = mode.getPhysicalWidth();
            heights[i] = mode.getPhysicalHeight();
        }

        return SecondaryDisplayTarget.select(ids, names, widths, heights);
    }

    /** The valid secondary display, or null while the emulator has not created it. */
    private Display secondaryDisplay() {
        int id = resolveSecondaryDisplayId();

        if (id < 0) {
            return null;
        }

        DisplayManager dm = (DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
        Display display = dm == null ? null : dm.getDisplay(id);

        return display != null && display.isValid() ? display : null;
    }
}
