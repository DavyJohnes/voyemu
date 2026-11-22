package com.qinggan.canbus.service;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.IBinder;
import android.os.Process;
import android.util.Log;

import java.lang.reflect.Method;

/**
 * Keeps the process alive and registers the stub binder in android.os.ServiceManager under the OEM
 * name "qg.canbus" (hidden API, invoked via reflection; works because this app is platform-signed
 * and shares android.uid.system).
 */
public class RegistrationService extends Service {
    private static final String TAG = "CanBusStub";
    private static final String SVC_NAME = "qg.canbus";
    private static volatile CanBusStubService sBinder;
    private static volatile boolean sRegistered;
    private HfpPhoneInfoBridge phoneBridge;

    /** The CAN bus component of the service; the base component has no vehicle model. */
    protected BaseCanBusComponent canBusComponent() {
        return new BaseCanBusComponent();
    }

    static org.json.JSONObject currentObservation(String request) throws org.json.JSONException {
        CanObservationRequest parsed = CanObservationRequest.parse(request);
        CanBusStubService binder = sBinder;

        return binder == null
                ? new org.json.JSONObject()
                        .put("schema", 1)
                        .put("source", "android:qg.canbus")
                        .put("status", "not-ready")
                : binder.observe(parsed);
    }

    static org.json.JSONObject phoneDiagnostics() throws org.json.JSONException {
        CanBusStubService binder = sBinder;

        return binder == null
                ? new org.json.JSONObject().put("schema", 1).put("status", "not-ready")
                : binder.phoneDiagnostics();
    }

    static org.json.JSONObject ipkHistory(String request) throws org.json.JSONException {
        CanBusStubService binder = sBinder;

        return binder == null
                ? new org.json.JSONObject()
                        .put("schema", 1)
                        .put("source", "android:qg.canbus:ipk")
                        .put("status", "not-ready")
                : binder.ipkHistory(request);
    }

    static org.json.JSONObject modeHistory(String request) throws org.json.JSONException {
        CanBusStubService binder = sBinder;

        return binder == null
                ? new org.json.JSONObject()
                        .put("schema", 1)
                        .put("source", "android:qg.canbus:mode")
                        .put("status", "not-ready")
                : binder.modeHistory(request);
    }

    static VehicleInputSnapshot currentInputs() {
        return sBinder == null ? null : sBinder.currentInputs();
    }

    private final BroadcastReceiver powerReceiver =
            new BroadcastReceiver() {
                @Override
                public void onReceive(Context context, Intent intent) {
                    if (sBinder == null || intent == null) {
                        return;
                    }

                    if (Intent.ACTION_SCREEN_OFF.equals(intent.getAction())) {
                        sBinder.setAccStatus(0);
                    } else if (Intent.ACTION_SCREEN_ON.equals(intent.getAction())) {
                        sBinder.setAccStatus(2);
                    }
                }
            };

    @Override
    public void onCreate() {
        super.onCreate();
        NotificationManager nm = getSystemService(NotificationManager.class);
        NotificationChannel ch =
                new NotificationChannel(
                        "canbus_stub", "CAN bus stub", NotificationManager.IMPORTANCE_MIN);
        nm.createNotificationChannel(ch);
        Notification n =
                new Notification.Builder(this, "canbus_stub")
                        .setContentTitle("qg.canbus emulator stub")
                        .setSmallIcon(android.R.drawable.stat_notify_sync_noanim)
                        .setOngoing(true)
                        .build();
        startForeground(1001, n);

        if (sBinder == null) {
            sBinder = new CanBusStubService(this, canBusComponent());

            try {
                Method add =
                        Class.forName("android.os.ServiceManager")
                                .getMethod("addService", String.class, IBinder.class);
                add.invoke(null, SVC_NAME, sBinder);
                sBinder.registrationObserved(true);
                sRegistered = true;
                Log.i(TAG, "registered " + SVC_NAME + " (uid " + Process.myUid() + ")");
            } catch (ReflectiveOperationException e) {
                sBinder.registrationObserved(false);
                sRegistered = false;
                Log.e(TAG, "failed to register " + SVC_NAME, e);
            }
        }

        if (sRegistered) {
            sBinder.attachModel();
            sBinder.registerClusterTheme();
            sBinder.startIpk();
            phoneBridge = new HfpPhoneInfoBridge(this, sBinder, sBinder.phoneDeliveryDiagnostics());
            phoneBridge.start();
        }

        IntentFilter power = new IntentFilter(Intent.ACTION_SCREEN_OFF);
        power.addAction(Intent.ACTION_SCREEN_ON);
        registerReceiver(powerReceiver, power);
    }

    @Override
    public void onDestroy() {
        if (phoneBridge != null) {
            phoneBridge.close();
        }

        if (sBinder != null) {
            sBinder.stopModelPolling();
        }

        unregisterReceiver(powerReceiver);
        super.onDestroy();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null
                && "com.voyemu.canbus.APPLY_PERSISTENT_VEHICLE_STATE".equals(intent.getAction())) {
            boolean applied =
                    sBinder != null
                            && sBinder.applyVehicleInputs(intent.getStringExtra("state_json"));
            Log.i(
                    TAG,
                    applied
                            ? "complete vehicle input queued or already current"
                            : "complete vehicle input rejected");

            // The host power state requests the ACC of the CAN model like the screen lifecycle.
            int acc = intent.getIntExtra("acc", -1);

            if (sBinder != null && (acc == 0 || acc == 2)) {
                sBinder.setAccStatus(acc);
            }
        }

        if (intent != null && "com.voyemu.canbus.APPLY_VEHICLE_STATE".equals(intent.getAction())) {
            String[] required = {
                "sequence", "speed_kmh", "soc_percent", "door_fl", "turn_left", "turn_right"
            };

            for (String key : required) {
                if (!intent.hasExtra(key)) {
                    return START_STICKY;
                }
            }

            boolean applied =
                    sBinder != null
                            && sBinder.applyVehicleSnapshot(
                                    intent.getLongExtra("sequence", -1),
                                            intent.getIntExtra("speed_kmh", -1),
                                    intent.getIntExtra("soc_percent", -1),
                                            intent.getIntExtra("door_fl", -1),
                                    intent.getIntExtra("turn_left", -1),
                                            intent.getIntExtra("turn_right", -1));
            Log.i(
                    TAG,
                    applied
                            ? "shared vehicle snapshot queued"
                            : "shared vehicle snapshot rejected");
        }

        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return sBinder;
    }
}
