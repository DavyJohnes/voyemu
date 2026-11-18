package com.voyemu.input;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.util.Log;

import java.lang.reflect.Method;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Binds the existing OEM audio component; it owns volume and real OEM callback delivery. */
public final class AudioControl {
    static final int WHEEL_FLAGS = 1024 | 3;
    private static final String PACKAGE = "com.qinggan.audiopolicy.service";
    private static final String ACTION = "com.qinggan.audiopolicy.action.AUDIO_POLICY_SERVICE";
    private final Context context;
    private final Object lock = new Object();
    private final Handler main = new Handler(Looper.getMainLooper());
    private volatile Object service;
    private volatile Class<?> api;
    private boolean binding;
    private String connectionState = "initial";
    private String failure = "not-attempted";
    private String operation = "none";
    private String errorClass = "";
    private String causeClass = "";
    private String loaderClass = "";
    private String proxyClass = "";
    private String policyInfoClass = "";
    private int connectedCallbacks;
    private int adjustmentAttempts;
    private boolean bindAccepted;
    private CountDownLatch ready = new CountDownLatch(1);

    public static final class VolumeChange {
        public final int stream;
        public final int before;
        public final int after;

        VolumeChange(int stream, int before, int after) {
            this.stream = stream;
            this.before = before;
            this.after = after;
        }
    }

    private final ServiceConnection connection =
            new ServiceConnection() {
                @Override
                public void onServiceConnected(ComponentName name, IBinder binder) {
                    synchronized (lock) {
                        connectedCallbacks++;
                        connectionState = "callback-entered";
                        operation = "create-package-context";
                    }

                    try {
                        // The owned audio APK contains its fetched OEM AIDL; oem-services does not
                        // duplicate it.
                        Context audio =
                                context.createPackageContext(
                                        PACKAGE,
                                        Context.CONTEXT_INCLUDE_CODE
                                                | Context.CONTEXT_IGNORE_SECURITY);

                        synchronized (lock) {
                            operation = "get-class-loader";
                        }

                        ClassLoader loader = audio.getClassLoader();

                        synchronized (lock) {
                            loaderClass =
                                    loader == null ? "bootstrap" : loader.getClass().getName();
                            operation = "load-api";
                        }

                        Class<?> contract =
                                Class.forName(
                                        "com.qinggan.audiopolicy.IAudioPolicyService",
                                        true,
                                        loader);

                        synchronized (lock) {
                            operation = "load-stub";
                        }

                        Class<?> stub =
                                Class.forName(
                                        "com.qinggan.audiopolicy.IAudioPolicyService$Stub",
                                        true,
                                        loader);

                        synchronized (lock) {
                            operation = "as-interface";
                        }

                        Object bound =
                                stub.getMethod("asInterface", IBinder.class).invoke(null, binder);

                        if (bound == null) {
                            throw new IllegalStateException("audio-binder-unavailable");
                        }

                        synchronized (lock) {
                            api = contract;
                            service = bound;
                            proxyClass = bound.getClass().getName();
                            connectionState = "bound";
                            failure = "none";
                            operation = "idle";
                            errorClass = "";
                            causeClass = "";
                            ready.countDown();
                        }
                    } catch (Exception | LinkageError error) {
                        // checkstyle-allow IllegalCatch: a failed OEM bind callback is a recorded
                        // bind failure
                        synchronized (lock) {
                            service = null;
                            api = null;
                            connectionState = "callback-failed";
                            ready.countDown();
                        }

                        recordFailure("callback-failed", error);
                        Log.w(
                                "VoyemuOemStub",
                                "audio bind failed " + error.getClass().getSimpleName());
                    }
                }

                @Override
                public void onServiceDisconnected(ComponentName name) {
                    synchronized (lock) {
                        service = null;
                        api = null;
                        ready = new CountDownLatch(1);
                        connectionState = "disconnected";
                        failure = "service-disconnected";
                    }
                }
            };

    public AudioControl(Context context) {
        this.context = context.getApplicationContext();
        warm();
    }

    public void warm() {
        synchronized (lock) {
            if (binding) {
                return;
            }

            binding = true;
            ready = new CountDownLatch(1);
            bindAccepted = false;
            connectionState = "bind-scheduled";
            operation = "bind-service";
        }

        main.post(() -> {
            try {
                boolean accepted =
                        context.bindService(
                                new Intent(ACTION).setPackage(PACKAGE),
                                connection,
                                Context.BIND_AUTO_CREATE);

                synchronized (lock) {
                    bindAccepted = accepted;

                    if (!accepted) {
                        binding = false;
                        connectionState = "bind-rejected";
                        failure = "bind-rejected";
                        ready.countDown();
                    } else if ("bind-scheduled".equals(connectionState)) {
                        connectionState = "bind-pending";
                    }
                }
            } catch (Exception error) {
                // checkstyle-allow IllegalCatch: a failed bind on the main looper is a recorded
                // bind failure
                synchronized (lock) {
                    binding = false;
                    connectionState = "bind-exception";
                    ready.countDown();
                }

                recordFailure("bind-exception", error);
                Log.w("VoyemuOemStub", "audio connect failed " + error.getClass().getSimpleName());
            }
        });
    }

    public Map<String, Object> diagnostics() {
        synchronized (lock) {
            Map<String, Object> value = new LinkedHashMap<>();
            value.put("connection_state", connectionState);
            value.put("bind_accepted", bindAccepted);
            value.put("connected_callbacks", connectedCallbacks);
            value.put("client_ready", service != null && api != null);
            value.put("adjustment_attempts", adjustmentAttempts);
            value.put("failure", failure);
            value.put("operation", operation);
            value.put("error_class", errorClass);
            value.put("cause_class", causeClass);
            value.put("loader_class", loaderClass);
            value.put("proxy_class", proxyClass);
            value.put("policy_info_class", policyInfoClass);

            return value;
        }
    }

    private void recordFailure(String reason, Throwable error) {
        Throwable cause = error.getCause();

        synchronized (lock) {
            failure = reason;
            errorClass = error.getClass().getName();
            causeClass = cause == null ? "" : cause.getClass().getName();
        }
    }

    private void checkpoint(String value) {
        synchronized (lock) {
            operation = value;
        }
    }

    static int policyStream(Object info) throws Exception {
        // AudioPolicyInfo.streamType is package-private in the actual A9 donor; use its public ABI.
        return info == null
                ? 3
                : ((Integer) info.getClass().getMethod("getStreamType").invoke(info)).intValue();
    }

    public boolean toggleMute() throws Exception {
        warm();
        final Object target = service;
        final Class<?> contract = api;

        // Hold timer runs on the main looper: never block waiting for a Binder connection.
        if (target == null || contract == null) {
            synchronized (lock) {
                failure = "client-unavailable";
            }

            return false;
        }

        try {
            checkpoint("query-master-mute");
            Method query = contract.getMethod("isMasterMute");
            boolean before = ((Boolean) query.invoke(target)).booleanValue();
            checkpoint("toggle-master-mute");
            contract.getMethod("setMasterMute", boolean.class, int.class)
                    .invoke(target, !before, 1);

            if (((Boolean) query.invoke(target)).booleanValue() == before) {
                throw new IllegalStateException("master-mute-readback-mismatch");
            }

            synchronized (lock) {
                failure = "none";
                operation = "idle";
                errorClass = "";
                causeClass = "";
            }

            return true;
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: records any OEM call failure for diagnostics, then
            // rethrows
            recordFailure("method-exception", error);
            throw error;
        }
    }

    /** Waits for the pending bind off the main looper; false records why it cannot. */
    private boolean awaitBinding(CountDownLatch pending) throws InterruptedException {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            synchronized (lock) {
                failure = "main-thread-await-refused";
            }

            return false;
        }

        if (!pending.await(1500, TimeUnit.MILLISECONDS)) {
            synchronized (lock) {
                failure = "binding-timeout";
            }

            return false;
        }

        return true;
    }

    private Object policyInfo(Object target, Class<?> contract) throws Exception {
        checkpoint("query-current-policy");
        Object info =
                contract.getMethod("getCurrentAudioPolicyInfo", int.class, String.class)
                        .invoke(target, 0, "com.voyemu.oem");

        if (info == null) {
            checkpoint("query-last-policy");
            info =
                    contract.getMethod("getLastMediaAudioPolicyInfoByChannel", int.class)
                            .invoke(target, 0);
        }

        synchronized (lock) {
            policyInfoClass = info == null ? "null" : info.getClass().getName();
        }

        return info;
    }

    public VolumeChange adjust(int direction) throws Exception {
        if (direction != -1 && direction != 1) {
            throw new IllegalArgumentException("bad-volume-direction");
        }

        synchronized (lock) {
            adjustmentAttempts++;
        }

        warm();
        final CountDownLatch pending;

        synchronized (lock) {
            pending = ready;
        }

        if (service == null && !awaitBinding(pending)) {
            return null;
        }

        final Object target = service;
        final Class<?> contract = api;

        if (target == null || contract == null) {
            synchronized (lock) {
                if ("none".equals(failure) || "not-attempted".equals(failure)) {
                    failure = "client-unavailable";
                }
            }

            return null;
        }

        try {
            Object info = policyInfo(target, contract);
            checkpoint("query-stream-type");
            int stream = policyStream(info);
            checkpoint("query-volume-before");
            Method getVolume = contract.getMethod("getStreamVolume", int.class, String.class);
            int before = ((Integer) getVolume.invoke(target, stream, "com.voyemu.oem")).intValue();
            checkpoint("adjust-volume");
            contract.getMethod("adjustVolume", int.class, int.class)
                    .invoke(target, direction, WHEEL_FLAGS);
            checkpoint("query-volume-after");
            int after = ((Integer) getVolume.invoke(target, stream, "com.voyemu.oem")).intValue();
            checkpoint("validate-volume-response");

            if (before < 0 || before > 30 || after < 0 || after > 30) {
                throw new IllegalStateException("invalid-volume-response");
            }

            synchronized (lock) {
                failure = "none";
                operation = "idle";
                errorClass = "";
                causeClass = "";
            }

            return new VolumeChange(stream, before, after);
        } catch (Exception error) {
            // checkstyle-allow IllegalCatch: records any OEM call failure for diagnostics, then
            // rethrows
            recordFailure("method-exception", error);
            throw error;
        }
    }
}
