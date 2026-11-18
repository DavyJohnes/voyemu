package com.voyemu.output;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.os.Handler;

import java.util.List;

/**
 * Executes the actual proposed service with deterministic Android boundaries; no guest or sleeps.
 */
public final class NxpBootstrapTest {
    static final class Owner extends SecondScreenService {
        final DisplayManager displays = new DisplayManager();
        int starts, activities, stops, failures, activityFailures;
        boolean throwsFailure;

        public Object getSystemService(String name) {
            need(Context.DISPLAY_SERVICE.equals(name), "unexpected service lookup");

            return displays;
        }

        public SharedPreferences getSharedPreferences(String name, int mode) {
            throw new AssertionError("audio boundary not isolated");
        }

        public ComponentName startService(Intent intent) {
            starts++;
            need(StartupAudioPolicy.calls > 0, "SystemUI precedes audio gate");
            ComponentName c = intent.getComponent();
            need(
                    c != null
                            && "com.qinggan.systemui".equals(c.getPackageName())
                            && "com.qinggan.systemui.SystemUIService".equals(c.getClassName()),
                    "wrong OEM component");

            if (failures > 0) {
                failures--;

                if (throwsFailure) {
                    throw new SecurityException("not ready");
                }

                return null;
            }

            return c;
        }

        public void startActivity(Intent intent, Bundle options) {
            activities++;
            need(starts > 0, "launcher precedes SystemUI request");
            ComponentName c = intent.getComponent();
            need(
                    c != null
                            && "com.qinggan.app.launcher".equals(c.getPackageName())
                            && "com.qinggan.secondlauncher.activity.SecondMainActivity"
                                    .equals(c.getClassName()),
                    "wrong second-home component");
            need(
                    "android.intent.action.MAIN".equals(intent.getAction())
                            && (intent.getFlags() & Intent.FLAG_ACTIVITY_NEW_TASK) != 0,
                    "second-home intent contract");
            need(options != null && options.displayId == 7, "second home routed to wrong display");

            if (activityFailures > 0) {
                activityFailures--;
                throw new IllegalStateException("launcher not ready");
            }
        }

        public void stopSelf() {
            stops++;
        }

        void start() {
            need(
                    onStartCommand(new Intent(), 0, 1) == START_NOT_STICKY,
                    "service restart contract changed");
        }
    }

    static void need(boolean ok, String message) {
        if (!ok) {
            throw new AssertionError(message);
        }
    }

    static Owner fresh() {
        Handler.QUEUE.clear();
        StartupAudioPolicy.calls = 0;
        StartupAudioPolicy.failures = 0;

        return new Owner();
    }

    static void next(long delay) {
        need(!Handler.QUEUE.isEmpty(), "missing callback");
        var pending = Handler.QUEUE.remove();
        need(pending.delay() == delay, "wrong callback delay");
        pending.task().run();
    }

    static void drain() {
        int callbacks = 0;

        while (!Handler.QUEUE.isEmpty()) {
            need(++callbacks < 200, "unbounded service callback loop");
            next(Handler.QUEUE.peek().delay());
        }
    }

    static void successAndExhaustion() {
        Owner success = fresh();

        success.start();
        need(
                success.starts == 1 && success.activities == 1 && success.stops == 0,
                "successful scheduling path");
        next(SecondScreenService.VERIFY_DELAY_MS);
        need(
                success.starts == 1 && success.stops == 1 && Handler.QUEUE.isEmpty(),
                "success repeated or not stopped");

        for (boolean thrown : List.of(false, true)) {
            Owner unavailable = fresh();
            unavailable.failures = 1000;
            unavailable.throwsFailure = thrown;
            unavailable.start();

            while (!Handler.QUEUE.isEmpty()) {
                next(SecondScreenService.RETRY_INTERVAL_MS);
            }

            need(
                    unavailable.starts == SecondScreenService.MAX_TRIES
                            && unavailable.stops == 1
                            && unavailable.activities == 0
                            && StartupAudioPolicy.calls == 1,
                    "SystemUI retry budget/exhaustion drift");
        }
    }

    static void serviceAndAudioRecovery() {
        Owner retry = fresh();

        retry.failures = 2;
        retry.start();
        next(1000);
        need(retry.starts == 2 && retry.activities == 0, "failed request advanced launcher");
        next(1000);
        need(retry.starts == 3 && retry.activities == 1, "transient failure did not recover");
        drain();
        need(retry.stops == 1 && retry.starts == 3, "recovery repeated start");
        Owner audio = fresh();
        StartupAudioPolicy.failures = 2;
        audio.start();
        need(audio.starts == 0, "failed audio gate leaked service start");
        next(1000);
        need(audio.starts == 0, "audio retry leaked service start");
        next(1000);
        need(audio.starts == 1 && StartupAudioPolicy.calls == 3, "audio recovery order");
        drain();
    }

    static void audioBudgetAndDisplays() {
        Owner failedAudio = fresh();

        StartupAudioPolicy.failures = 1000;
        failedAudio.start();
        drain();
        need(
                failedAudio.starts == 0
                        && failedAudio.stops == 1
                        && StartupAudioPolicy.calls == SecondScreenService.MAX_TRIES,
                "audio retry budget drift");
        Owner displays = fresh();
        displays.displays.available = false;
        displays.start();
        next(1000);
        next(1000);
        need(displays.starts == 1 && displays.activities == 0, "display retries repeated SystemUI");
        displays.displays.available = true;
        next(1000);
        drain();
        need(
                displays.starts == 1 && displays.activities == 1 && displays.stops == 1,
                "late display did not recover");
        Owner noDisplay = fresh();
        noDisplay.displays.available = false;
        noDisplay.start();
        drain();
        need(
                noDisplay.starts == 1 && noDisplay.activities == 0 && noDisplay.stops == 1,
                "missing display not bounded");
    }

    static void launcherAndLostDisplay() {
        Owner launchRetry = fresh();

        launchRetry.activityFailures = 2;
        launchRetry.start();
        next(1000);
        next(1000);
        drain();
        need(
                launchRetry.starts == 1 && launchRetry.activities == 3 && launchRetry.stops == 1,
                "launcher transient failure did not recover");
        Owner launchFailure = fresh();
        launchFailure.activityFailures = 1000;
        launchFailure.start();
        drain();
        need(
                launchFailure.starts == 1
                        && launchFailure.activities == SecondScreenService.MAX_TRIES + 1
                        && launchFailure.stops == 1,
                "launcher retries not bounded");
        Owner lostDisplay = fresh();
        lostDisplay.start();
        lostDisplay.displays.available = false;
        next(SecondScreenService.VERIFY_DELAY_MS);
        need(
                lostDisplay.stops == 0 && lostDisplay.activities == 1,
                "lost display incorrectly accepted");
        lostDisplay.displays.available = true;
        next(1000);
        drain();
        need(
                lostDisplay.starts == 1 && lostDisplay.activities == 2 && lostDisplay.stops == 1,
                "lost display did not recover");
    }

    static void generationLifecycle() {
        Owner generation = fresh();

        generation.failures = 2;
        generation.start();
        Runnable stale = Handler.QUEUE.remove().task();
        generation.start();
        int calls = generation.starts;
        stale.run();
        need(generation.starts == calls && Handler.QUEUE.size() == 1, "stale generation executed");
        drain();
        need(generation.starts == 3 && generation.stops == 1, "new generation not recovered");
        Owner destroyed = fresh();
        destroyed.failures = 3;
        destroyed.start();
        Runnable expired = Handler.QUEUE.peek().task();
        destroyed.onDestroy();
        need(Handler.QUEUE.isEmpty(), "destroy did not cancel callbacks");
        expired.run();
        need(destroyed.starts == 1 && Handler.QUEUE.isEmpty(), "destroyed callback executed");
        Owner duplicate = fresh();
        duplicate.start();
        duplicate.start();
        need(
                duplicate.starts == 1 && duplicate.activities == 1 && duplicate.stops == 1,
                "duplicate start repeated OEM service or launcher");
        drain();
        need(duplicate.starts == 1 && duplicate.stops == 1, "expired verify repeated stop");
    }

    public static void main(String[] arguments) {
        successAndExhaustion();
        serviceAndAudioRecovery();
        audioBudgetAndDisplays();
        launcherAndLostDisplay();
        generationLifecycle();
        System.out.println(
                "PASS NXP bootstrap: success/null/throw/bounded exhaustion/transient recovery/audio"
                        + " order/display delay/stale generation/destroy/duplicate");
    }
}
