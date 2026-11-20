package com.voyemu.oem;

import android.content.Context;
import android.content.Intent;
import android.media.AudioManager;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;
import android.view.KeyEvent;

import com.qinggan.system.AreaFocusModel;
import com.qinggan.system.IDirectionEventListener;
import com.qinggan.system.IKeyManagerCallback;
import com.qinggan.system.QGKeyEvent;
import com.voyemu.input.AudioControl;
import com.voyemu.input.KeyEventRouter;
import com.voyemu.input.WheelInputSession;

import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

/** Restores the matching OEM callback ABI; transient wheel events never enter vehicle state. */
public class KeyManagerStub extends com.qinggan.system.IKeyManagerService.Stub {
    private static final String TAG = "VoyemuOemStub";
    private final KeyEventRouter router = new KeyEventRouter();
    private final Map<String, Registration> registrations = new LinkedHashMap<>();
    private final Map<String, DirectionRegistration> directions = new LinkedHashMap<>();
    private final Map<String, AreaFocusModel> areas = new LinkedHashMap<>();
    private AreaFocusModel focused;
    private Context context;
    private AudioControl audioTerminal;
    private final ThreadPoolExecutor callbacks =
            new ThreadPoolExecutor(
                    2,
                    2,
                    0L,
                    TimeUnit.MILLISECONDS,
                    new ArrayBlockingQueue<Runnable>(32),
                    task -> {
                        Thread thread = new Thread(task, "voyemu-wheel-callback");
                        thread.setDaemon(true);

                        return thread;
                    });

    public synchronized void attach(Context value) {
        context = value.getApplicationContext();

        if (audioTerminal == null) {
            audioTerminal = new AudioControl(context);
        } else {
            audioTerminal.warm();
        }
    }

    private static String id(String pkg, int type) {
        return pkg + "#" + type;
    }

    private final class Registration implements IBinder.DeathRecipient, KeyEventRouter.Callback {
        final IBinder token;
        final IKeyManagerCallback callback;
        final String pkg;
        final int type;

        Registration(IBinder token, IKeyManagerCallback callback, String pkg, int type) {
            this.token = token;
            this.callback = callback;
            this.pkg = pkg;
            this.type = type;
        }

        @Override
        public void binderDied() {
            remove(this);
        }

        @Override
        public boolean onEvent(int code, int action, String registration, int listenerType)
                throws Exception {
            Future<Boolean> pending = null;
            boolean answered = false;

            try {
                // onKeyEvent() internally fans out every client listener and ignores registration.
                pending =
                        callbacks.submit(() ->
                                callback.onEvent(
                                        new QGKeyEvent(code, action), registration, listenerType));
                boolean consumed = pending.get(1500, TimeUnit.MILLISECONDS);
                answered = true;

                return consumed;
            } finally {
                if (!answered) {
                    if (pending != null) {
                        pending.cancel(true);
                    }

                    remove(this);
                }
            }
        }

        void unlink() {
            unlinkBinder(token);

            if (!token.equals(callback.asBinder())) {
                unlinkBinder(callback.asBinder());
            }
        }

        private void unlinkBinder(IBinder binder) {
            try {
                binder.unlinkToDeath(this, 0);
            } catch (java.util.NoSuchElementException alreadyDead) {
                /* Death already removed it. */
            }
        }
    }

    private synchronized void remove(Registration registration) {
        String key = id(registration.pkg, registration.type);

        if (registrations.get(key) != registration) {
            return;
        }

        registrations.remove(key);
        router.unregister(registration.pkg, registration.type, registration);
        registration.unlink();
    }

    private synchronized int register(
            IBinder token,
            IKeyManagerCallback callback,
            int[] keys,
            String pkg,
            int flags,
            int priority,
            int type) {
        if (token == null
                || callback == null
                || callback.asBinder() == null
                || pkg == null
                || pkg.length() == 0
                || pkg.length() > 256
                || (keys != null && keys.length > 256)) {
            return -1;
        }

        Registration registration = new Registration(token, callback, pkg, type);

        try {
            token.linkToDeath(registration, 0);

            if (!token.equals(callback.asBinder())) {
                callback.asBinder().linkToDeath(registration, 0);
            }
        } catch (RemoteException error) {
            registration.unlink();

            return -1;
        }

        Registration previous = registrations.get(id(pkg, type));

        if (!router.register(pkg, type, priority, keys, registration)) {
            registration.unlink();

            return -1;
        }

        registrations.put(id(pkg, type), registration);

        if (previous != null) {
            previous.unlink();
        }

        Log.i(TAG, "wheel listener pkg=" + pkg + " type=" + type + " priority=" + priority);

        return 0;
    }

    private synchronized int unregister(IKeyManagerCallback callback, String pkg, int type) {
        Registration registration = registrations.get(id(pkg, type));

        if (registration != null
                && callback != null
                && registration.callback.asBinder().equals(callback.asBinder())) {
            remove(registration);
        }

        return 0;
    }

    @Override
    public int registerKeyManagerListener(
            IBinder token,
            IKeyManagerCallback cb,
            int[] keys,
            String pkg,
            int flags,
            int priority) {
        return register(token, cb, keys, pkg, flags, priority, 0);
    }

    @Override
    public int unregisterKeyManagerListener(IKeyManagerCallback cb, String pkg) {
        return unregister(cb, pkg, 0);
    }

    @Override
    public int registerInterceptListener(
            IBinder token,
            IKeyManagerCallback cb,
            int[] keys,
            String pkg,
            int flags,
            int priority) {
        return register(token, cb, keys, pkg, flags, priority, 1);
    }

    @Override
    public int unregisterInterceptListener(IKeyManagerCallback cb, String pkg) {
        return unregister(cb, pkg, 1);
    }

    public KeyEventRouter.Delivery dispatchQG(int keyCode, int action) {
        KeyEventRouter.Delivery delivery = router.dispatch(keyCode, action);
        int translated = router.mapping(keyCode);

        if (!delivery.restricted
                && !delivery.consumed
                && (delivery.callbacks == 0
                        || translated == 130
                        || translated == 1
                        || translated == 2
                        // Wildcard launcher/system listeners are notifications, not the SRC
                        // consumer.
                        || (translated == 65 && !delivery.srcNotified))
                && (action == WheelInputSession.SHORT
                        || (action == WheelInputSession.LONG
                                && (translated == 3090 || translated == 1)))
                && terminal(translated, action, delivery)) {
            delivery.consumed = true;
            delivery.terminals++;
        }

        return delivery;
    }

    public static String deliveryStatus(KeyEventRouter.Delivery delivery) {
        // OEM camera/phone callbacks may perform their action and return false.
        return delivery.restricted
                ? "restricted"
                : delivery.terminalFailure != null
                        ? "terminal-unavailable"
                        : delivery.callbacks > 0
                                ? "callbacks"
                                : delivery.terminals > 0 ? "terminal-requested" : "no-consumer";
    }

    public Map<String, Object> audioDiagnostics() {
        Map<String, Object> value =
                audioTerminal == null ? new LinkedHashMap<>() : audioTerminal.diagnostics();

        value.put("context_attached", context != null);
        value.put("terminal_present", audioTerminal != null);

        return value;
    }

    private boolean terminal(int keyCode, int action, KeyEventRouter.Delivery delivery) {
        if (context == null) {
            noteUnattached(keyCode, delivery);

            return false;
        }

        try {
            if (keyCode == 65 && action == WheelInputSession.SHORT) {
                return startSrc(delivery);
            }

            if (keyCode == 3090 && action == WheelInputSession.LONG) {
                // TranslatorSink maps the default star to SRC even for LONG; this applies only when
                // unassigned.
                context.sendBroadcast(new Intent("com.qinggan.app.setting.STEERING_WHEEL_SETTING"));

                return true;
            }

            if (keyCode == 130) {
                // Matching KtQGSink.IVokaStart, rather than a generic Android voice shortcut.
                context.sendBroadcast(new Intent("com.qinggan.keymanager.intent.ivoka"));

                return true;
            }

            if (keyCode == 1 && action == WheelInputSession.LONG) {
                return toggleMute(delivery);
            }

            if (keyCode == 1 || keyCode == 2) {
                return adjustVolume(keyCode, delivery);
            }

            return mediaKey(keyCode);
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: any failed terminal action is a recorded terminal
            // failure
            delivery.terminalFailure = "method-exception";

            if (keyCode == 1 || keyCode == 2) {
                delivery.terminalDiagnostics = audioDiagnostics();
            }

            Log.w(
                    TAG,
                    "wheel terminal unavailable code="
                            + keyCode
                            + " error="
                            + error.getClass().getSimpleName());
        }

        return false;
    }

    private void noteUnattached(int keyCode, KeyEventRouter.Delivery delivery) {
        if (keyCode == 1 || keyCode == 2) {
            delivery.terminalFailure = "context-unattached";
            delivery.terminalDiagnostics = audioDiagnostics();
        } else if (keyCode == 65 || keyCode == 3090) {
            delivery.terminalFailure = "context-unattached";
        }
    }

    private boolean startSrc(KeyEventRouter.Delivery delivery) throws Exception {
        // Matching OEM MediaKeyManager.onHandleSrc/startSrcActivity. Both APKs share system
        // UID.
        Method property =
                Class.forName("android.os.SystemProperties")
                        .getMethod("get", String.class, String.class);

        if ("on".equals(property.invoke(null, "com.qinggan.restmode.state", "off"))
                || "on".equals(property.invoke(null, "com.qinggan.campmode.state", "off"))) {
            delivery.terminalFailure = "src-mode-blocked";

            return false;
        }

        Intent intent =
                new Intent("android.intent.action.MAIN")
                        .setClassName(
                                "com.qinggan.media",
                                "com.qingang.asgard.media.general.src.SrcMediaActivity")
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        context.startActivity(intent);

        return true;
    }

    private boolean toggleMute(KeyEventRouter.Delivery delivery) throws Exception {
        // User-requested hold-minus policy; OEM QG5 terminal toggles master mute.
        boolean changed = audioTerminal != null && audioTerminal.toggleMute();

        delivery.terminalDiagnostics = audioDiagnostics();

        if (!changed) {
            delivery.terminalFailure =
                    audioTerminal == null
                            ? "terminal-unattached"
                            : String.valueOf(delivery.terminalDiagnostics.get("failure"));
        }

        return changed;
    }

    private boolean adjustVolume(int keyCode, KeyEventRouter.Delivery delivery) throws Exception {
        // Matching KtVolumeAdjust.adjustVol -> the existing OEM IAudioPolicyService.
        AudioControl.VolumeChange change =
                audioTerminal == null ? null : audioTerminal.adjust(keyCode == 2 ? 1 : -1);

        delivery.terminalDiagnostics = audioDiagnostics();

        if (change == null) {
            delivery.terminalFailure =
                    audioTerminal == null
                            ? "terminal-unattached"
                            : String.valueOf(delivery.terminalDiagnostics.get("failure"));

            return false;
        }

        delivery.volume = change;

        return true;
    }

    private boolean mediaKey(int keyCode) {
        int androidCode =
                keyCode == 3
                        ? KeyEvent.KEYCODE_MEDIA_NEXT
                        : keyCode == 4
                                ? KeyEvent.KEYCODE_MEDIA_PREVIOUS
                                : keyCode == 6 ? KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE : 0;

        if (androidCode != 0) {
            // Matching OEM KtAndroidSink terminal after no OEM media registration exists.
            AudioManager audio = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);

            if (audio == null) {
                return false;
            }

            audio.dispatchMediaKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, androidCode));
            audio.dispatchMediaKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, androidCode));

            return true;
        }

        return false;
    }

    @Override
    public void inputKeyEvent(int keyCode, int flags, String pkg) {
        // OEM second argument is a pulse mode, not a QG action. Long modes need the reader timer.
        if (flags != 0 && flags != 1) {
            Log.w(TAG, "inputKeyEvent unsupported pulse mode=" + flags);

            return;
        }

        dispatchQG(keyCode, WheelInputSession.DOWN);
        dispatchQG(keyCode, WheelInputSession.SHORT);
        dispatchQG(keyCode, WheelInputSession.UP);
    }

    @Override
    public void inputQGKeyEvent(QGKeyEvent event, String pkg) {
        if (event != null) {
            dispatchQG(event.getKeyCode(), event.getAction());
        }
    }

    @Override
    public void mappingKeyEvent(int from, int to, String pkg) {
        router.mapping(from, to);
    }

    @Override
    public int getMappingKeyEvent(int code, String pkg) {
        return router.mapping(code);
    }

    @Override
    public void setRestriction(boolean restrict, int[] keys) {
        router.restriction(restrict, keys);
    }

    private final class DirectionRegistration implements IBinder.DeathRecipient {
        final IDirectionEventListener callback;
        final String pkg;

        DirectionRegistration(IDirectionEventListener callback, String pkg) {
            this.callback = callback;
            this.pkg = pkg;
        }

        @Override
        public void binderDied() {
            synchronized (KeyManagerStub.this) {
                if (directions.get(pkg) == this) {
                    directions.remove(pkg);
                }
            }
        }
    }

    @Override
    public synchronized boolean registerDirectionEventListener(
            IDirectionEventListener listener, String pkg) {
        if (listener == null || pkg == null || pkg.length() == 0 || directions.size() >= 128) {
            return false;
        }

        DirectionRegistration registration = new DirectionRegistration(listener, pkg);

        try {
            listener.asBinder().linkToDeath(registration, 0);
        } catch (RemoteException error) {
            return false;
        }

        DirectionRegistration previous = directions.put(pkg, registration);

        if (previous != null) {
            previous.callback.asBinder().unlinkToDeath(previous, 0);
        }

        return true;
    }

    @Override
    public synchronized boolean unregisterDirectionEventListener(
            IDirectionEventListener listener, String pkg) {
        DirectionRegistration previous = directions.get(pkg);

        if (previous != null
                && listener != null
                && previous.callback.asBinder().equals(listener.asBinder())) {
            directions.remove(pkg);
            previous.callback.asBinder().unlinkToDeath(previous, 0);
        }

        return true;
    }

    private static String areaId(AreaFocusModel area, String pkg) {
        return pkg + "#" + area.areaId;
    }

    @Override
    public synchronized boolean addFocusArea(AreaFocusModel area, String pkg) {
        if (area == null || pkg == null || !pkg.equals(area.packageName) || areas.size() >= 128) {
            return false;
        }

        areas.put(areaId(area, pkg), area);

        return true;
    }

    @Override
    public synchronized boolean removeFocusArea(AreaFocusModel area, String pkg) {
        if (area == null || pkg == null) {
            return false;
        }

        AreaFocusModel removed = areas.remove(areaId(area, pkg));

        if (focused == removed) {
            focused = null;
        }

        return removed != null;
    }

    @Override
    public boolean requestAreaFocus(AreaFocusModel area, String pkg) {
        final AreaFocusModel previous;
        final ArrayList<DirectionRegistration> listeners;

        synchronized (this) {
            if (area == null || !areas.containsKey(areaId(area, pkg))) {
                return false;
            }

            previous = focused;
            focused = areas.get(areaId(area, pkg));
            listeners = new ArrayList<>(directions.values());
        }

        for (DirectionRegistration listener : listeners) {
            try {
                listener.callback.onAreaFocusChanged(previous, area, null);
            } catch (RemoteException error) {
                listener.binderDied();
            }
        }

        return true;
    }

    @Override
    public synchronized void updateAreaFocus(int direction, int[] focusOrder, String pkg) {
        if (focusOrder == null || focusOrder.length != 4 || pkg == null) {
            return;
        }

        AreaFocusModel area = areas.get(pkg + "#" + direction);

        if (area != null) {
            area.topFocusArea = focusOrder[0];
            area.bottomFocusArea = focusOrder[1];
            area.leftFocusArea = focusOrder[2];
            area.rightFocusArea = focusOrder[3];
        }
    }

    @Override
    public void focusAsSystem(boolean focus, String pkg) {
        // Direction focus requests are explicit; unrelated media ownership is not changed here.
    }
}
