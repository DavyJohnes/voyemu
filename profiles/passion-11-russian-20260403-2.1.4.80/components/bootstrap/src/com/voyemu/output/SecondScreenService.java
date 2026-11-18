package com.voyemu.output;

import android.app.ActivityOptions;
import android.app.Service;
import android.content.ComponentName;
import android.content.Intent;
import android.hardware.display.DisplayManager;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.UserManager;
import android.util.Log;
import android.view.Display;

/** Requests owner secondary HOME; external window/frame checks decide actual readiness. */
public final class SecondScreenService extends Service {
    private static final String TAG = "VoyemuBootstrap";
    private static final int MAX_ATTEMPTS = 60;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable retry = this::attempt;
    private int remaining;
    private boolean startupAudioZero;

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        handler.removeCallbacks(retry);
        startupAudioZero = false;
        remaining = MAX_ATTEMPTS;
        attempt();

        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        handler.removeCallbacks(retry);
        super.onDestroy();
    }

    private void attempt() {
        if (remaining-- <= 0) {
            Log.e(TAG, "Secondary HOME request exhausted bounded retries");
            stopSelf();

            return;
        }

        UserManager user = (UserManager) getSystemService(USER_SERVICE);

        if (user == null || !user.isUserUnlocked()) {
            handler.postDelayed(retry, 1000);

            return;
        }

        if (!startupAudioZero) {
            try {
                StartupAudioPolicy.zero(this);
                startupAudioZero = true;
            } catch (RuntimeException failure) {
                // checkstyle-allow IllegalCatch: Android startup failures retain bounded retry
                // behavior.
                Log.w(TAG, "Startup audio reset failed", failure);
                handler.postDelayed(retry, 1000);

                return;
            }
        }

        DisplayManager manager = (DisplayManager) getSystemService(DISPLAY_SERVICE);
        Display[] displays = manager == null ? new Display[0] : manager.getDisplays();
        int[] ids = new int[displays.length];
        String[] names = new String[displays.length];

        for (int i = 0; i < displays.length; i++) {
            ids[i] = displays[i].isValid() ? displays[i].getDisplayId() : -1;
            names[i] = displays[i].getName();
        }

        int id = SecondaryDisplayTarget.select(ids, names);

        if (id >= 0) {
            try {
                Intent home =
                        new Intent(Intent.ACTION_MAIN)
                                .addCategory(Intent.CATEGORY_SECONDARY_HOME)
                                .setComponent(
                                        new ComponentName(
                                                SecondaryDisplayTarget.PACKAGE,
                                                SecondaryDisplayTarget.ACTIVITY))
                                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                ActivityOptions options = ActivityOptions.makeBasic();
                options.setLaunchDisplayId(id);
                startActivity(home, options.toBundle());
                Log.i(TAG, "Secondary HOME request dispatched on display " + id);
                stopSelf();

                return;
            } catch (RuntimeException failure) {
                // checkstyle-allow IllegalCatch: Android startup failures retain bounded retry
                // behavior.
                Log.w(TAG, "Secondary HOME request failed", failure);
            }
        }

        handler.postDelayed(retry, 1000);
    }
}
