package com.voyemu.output;

import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.util.ArrayList;
import java.util.WeakHashMap;

/** Defer the original OEM navigation factory until its two real displays exist. */
public final class PassionNavigationReady {
    private static Job pending;

    static final class Requests {
        final WeakHashMap<Object, Boolean> services = new WeakHashMap<>();
        boolean activity;

        void add(Object service) {
            if (service == null) {
                activity = true;
            } else {
                services.put(service, Boolean.TRUE);
            }
        }

        void remove(Object service) {
            services.remove(service);
        }

        void cancel(Object service, Job job) {
            remove(service);

            if (empty() && job != null) {
                job.stop();
            }
        }

        boolean empty() {
            return !activity && services.isEmpty();
        }

        void clear() {
            activity = false;
            services.clear();
        }
    }

    private static final Requests REQUESTS = new Requests();

    private PassionNavigationReady() {}

    interface Environment {
        boolean ready();

        void listen(Runnable changed);

        void unlisten();

        void deadline(Runnable expired);

        void cancelDeadline();

        void showOriginal();

        void unavailable();
    }

    static final class Job {
        final Environment environment;
        final Runnable finished;
        boolean stopped;

        Job(Environment environment, Runnable finished) {
            this.environment = environment;
            this.finished = finished;
        }

        void start() {
            try {
                if (environment.ready()) {
                    stop();
                    environment.showOriginal();

                    return;
                }

                environment.listen(this::changed);
                environment.deadline(this::expire);
                // Recheck after registration, closing the display-add/register race.
                changed();
            } catch (RuntimeException | Error failure) {
                // checkstyle-allow IllegalCatch: Callback cleanup must retain all failures and the
                // original suppressed-error ordering.
                try {
                    stop();
                } catch (RuntimeException | Error cleanup) {
                    // checkstyle-allow IllegalCatch: Callback cleanup must retain all failures and
                    // the original suppressed-error ordering.
                    failure.addSuppressed(cleanup);
                }

                throw failure;
            }
        }

        void changed() {
            try {
                if (!stopped && environment.ready()) {
                    stop();
                    environment.showOriginal();
                }
            } catch (RuntimeException | Error failure) {
                // checkstyle-allow IllegalCatch: Callback cleanup must retain all failures and the
                // original suppressed-error ordering.
                try {
                    stop();
                } catch (RuntimeException | Error cleanup) {
                    // checkstyle-allow IllegalCatch: Callback cleanup must retain all failures and
                    // the original suppressed-error ordering.
                    failure.addSuppressed(cleanup);
                }

                throw failure;
            }
        }

        void expire() {
            if (!stopped) {
                stop();
                environment.unavailable();
            }
        }

        void stop() {
            if (stopped) {
                return;
            }

            stopped = true;

            try {
                environment.unlisten();
            } finally {
                try {
                    environment.cancelDeadline();
                } finally {
                    finished.run();
                }
            }
        }
    }

    private static Context application(Context context) {
        if (context == null
                || Build.VERSION.SDK_INT != 30
                || !"com.voyah.os.carlauncher".equals(context.getPackageName())) {
            throw new IllegalArgumentException("Pinned Passion navigation context required");
        }

        Context application = context.getApplicationContext();

        if (application == null) {
            throw new IllegalStateException("OEM navigation requires non-null application Context");
        }

        return application;
    }

    public static void show(Context context) {
        request(context, false);
    }

    public static void showService(Context service) {
        request(service, true);
    }

    private static void request(final Context context, final boolean service) {
        final Context owner = application(context);
        final Handler main = new Handler(Looper.getMainLooper());

        if (Looper.myLooper() != Looper.getMainLooper()) {
            main.post(() -> request(context, service));

            return;
        }

        if (service
                && !"com.voyah.os.carsystemui.SystemuiService"
                        .equals(context.getClass().getName())) {
            throw new IllegalArgumentException("Exact OEM SystemuiService required");
        }

        REQUESTS.add(service ? context : null);

        if (pending != null) {
            return;
        }

        try {
            AndroidEnvironment environment = new AndroidEnvironment(owner, main);
            Job job = new Job(environment, () -> pending = null);
            pending = job;
            job.start();
        } catch (RuntimeException | Error failure) {
            // checkstyle-allow IllegalCatch: Callback cleanup must retain all failures and the
            // original suppressed-error ordering.
            REQUESTS.clear();
            throw failure;
        }
    }

    public static void destroyService(final Context service) {
        application(service);
        final Handler main = new Handler(Looper.getMainLooper());

        if (Looper.myLooper() != Looper.getMainLooper()) {
            main.post(() -> destroyService(service));

            return;
        }

        REQUESTS.cancel(service, pending);

        try {
            Field field = serviceField(service);
            Object manager = field.get(service);
            // Clear before invoking so a repeated destroy cannot invoke the manager twice.
            field.set(service, null);

            if (manager != null) {
                invoke(manager, "onDestroy");
            }
        } catch (ReflectiveOperationException failure) {
            throw new IllegalStateException(
                    "Pinned OEM navigation service field unavailable", failure);
        }
    }

    private static Field serviceField(Object service) throws ReflectiveOperationException {
        Class<?> type =
                Class.forName(
                        "com.voyah.os.carsystemui.SystemuiService",
                        true,
                        service.getClass().getClassLoader());

        if (service.getClass() != type) {
            throw new IllegalArgumentException("Exact OEM SystemuiService required");
        }

        Field field = type.getDeclaredField("navigationBarManager");
        field.setAccessible(true);

        return field;
    }

    private static void invoke(Object manager, String method) throws ReflectiveOperationException {
        try {
            manager.getClass().getMethod(method).invoke(manager);
        } catch (InvocationTargetException failure) {
            Throwable cause = failure.getCause();

            if (cause instanceof RuntimeException) {
                throw (RuntimeException) cause;
            }

            if (cause instanceof Error) {
                throw (Error) cause;
            }

            throw new IllegalStateException("OEM navigation invocation failed", cause);
        }
    }

    private static final class AndroidEnvironment implements Environment {
        final Context context;
        final Handler main;
        final DisplayManager displays;
        DisplayManager.DisplayListener listener;
        Runnable timeout;

        AndroidEnvironment(Context context, Handler main) {
            this.context = context;
            this.main = main;
            displays = (DisplayManager) context.getSystemService(Context.DISPLAY_SERVICE);

            if (displays == null) {
                throw new IllegalStateException("DisplayManager unavailable");
            }
        }

        @Override
        public boolean ready() {
            return displays.getDisplay(0) != null && displays.getDisplay(1) != null;
        }

        @Override
        public void listen(final Runnable changed) {
            listener =
                    new DisplayManager.DisplayListener() {
                        @Override
                        public void onDisplayAdded(int id) {
                            changed.run();
                        }

                        @Override
                        public void onDisplayChanged(int id) {
                            changed.run();
                        }

                        @Override
                        public void onDisplayRemoved(int id) {
                            changed.run();
                        }
                    };
            displays.registerDisplayListener(listener, main);
        }

        @Override
        public void unlisten() {
            if (listener != null) {
                displays.unregisterDisplayListener(listener);
                listener = null;
            }
        }

        @Override
        public void deadline(Runnable expired) {
            timeout = expired;
            main.postDelayed(expired, 30000);
        }

        @Override
        public void cancelDeadline() {
            if (timeout != null) {
                main.removeCallbacks(timeout);
                timeout = null;
            }
        }

        @Override
        public void unavailable() {
            REQUESTS.clear();
            Log.w(
                    "PassionNavigationReady",
                    "OEM navigation unavailable: real displays 0/1 not ready within 30s");
        }

        @Override
        public void showOriginal() {
            try {
                Class<?> type =
                        Class.forName(
                                "com.voyah.os.carsystemui.navigation.NavigationBarManager",
                                true,
                                context.getClassLoader());
                Object manager = type.getMethod("getInstance", Context.class).invoke(null, context);

                if (manager == null) {
                    throw new IllegalStateException(
                            "OEM navigation factory returned null after real display readiness");
                }

                for (Object service : new ArrayList<Object>(REQUESTS.services.keySet())) {
                    serviceField(service).set(service, manager);
                }

                invoke(manager, "showNavigationBar");
            } catch (InvocationTargetException failure) {
                Throwable cause = failure.getCause();

                if (cause instanceof RuntimeException) {
                    throw (RuntimeException) cause;
                }

                if (cause instanceof Error) {
                    throw (Error) cause;
                }

                throw new IllegalStateException("OEM navigation invocation failed", cause);
            } catch (ReflectiveOperationException failure) {
                throw new IllegalStateException(
                        "Pinned OEM navigation contract unavailable", failure);
            } finally {
                REQUESTS.clear();
            }
        }
    }
}
