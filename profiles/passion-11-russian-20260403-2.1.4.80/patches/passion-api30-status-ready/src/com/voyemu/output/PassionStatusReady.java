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

/** Defer the original OEM status factory until its two real displays exist. */
public final class PassionStatusReady {
    private static Job pending;

    static final class Command {
        boolean initialize, visible;

        Command(boolean initialize, boolean visible) {
            this.initialize = initialize;
            this.visible = visible;
        }
    }

    static final class Requests {
        final WeakHashMap<Object, Command> services = new WeakHashMap<>();
        final WeakHashMap<Object, Boolean> destroyed = new WeakHashMap<>();
        final WeakHashMap<Object, Boolean> initializations = new WeakHashMap<>();
        boolean activity;

        void prepare() {
            activity = true;
        }

        void add(Object service, boolean initialize, boolean visible) {
            Command old = services.get(service);

            services.put(
                    service, new Command(initialize || (old != null && old.initialize), visible));
        }

        void remove(Object service) {
            services.remove(service);
        }

        void cancel(Object service, Job job) {
            destroyed.put(service, Boolean.TRUE);
            initializations.remove(service);
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

    private PassionStatusReady() {}

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
            throw new IllegalArgumentException("Pinned Passion status context required");
        }

        Context application = context.getApplicationContext();

        if (application == null) {
            throw new IllegalStateException("OEM status requires non-null application Context");
        }

        return application;
    }

    public static void prepare(final Context context) {
        final Context owner = application(context);
        final Handler main = new Handler(Looper.getMainLooper());

        if (Looper.myLooper() != Looper.getMainLooper()) {
            main.post(() -> prepare(context));

            return;
        }

        REQUESTS.prepare();
        ensure(owner, main);
    }

    public static void startService(Context service) {
        request(service, true, true, true);
    }

    public static void showService(Context service, boolean initialize) {
        request(service, initialize, true, false);
    }

    public static void hideService(Context service) {
        request(service, false, false, false);
    }

    private static void request(
            final Context service,
            final boolean initialize,
            final boolean visible,
            final boolean starting) {
        final Context owner = application(service);
        final Handler main = new Handler(Looper.getMainLooper());

        if (Looper.myLooper() != Looper.getMainLooper()) {
            main.post(() -> request(service, initialize, visible, starting));

            return;
        }

        if (starting) {
            REQUESTS.destroyed.remove(service);
            REQUESTS.initializations.put(service, Boolean.TRUE);
        } else if (REQUESTS.destroyed.containsKey(service)) {
            Log.w("PassionStatusReady", "Ignoring status callback after service destruction");

            return;
        }

        boolean mustInitialize = initialize || REQUESTS.initializations.containsKey(service);

        try {
            Object manager = serviceField(service).get(service);

            if (manager != null) {
                if (mustInitialize) {
                    show(manager, true);
                    REQUESTS.initializations.remove(service);
                }

                if (!visible) {
                    invoke(manager, "removeStatusBar");
                } else if (!mustInitialize) {
                    show(manager, false);
                }

                return;
            }
        } catch (ReflectiveOperationException failure) {
            throw new IllegalStateException("Pinned OEM status service field unavailable", failure);
        }

        REQUESTS.add(service, mustInitialize, visible);
        ensure(owner, main);
    }

    private static void ensure(Context owner, Handler main) {
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
            throw new IllegalStateException("Pinned OEM status service field unavailable", failure);
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

        Field field = type.getDeclaredField("statusBarManager");
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

            throw new IllegalStateException("OEM status invocation failed", cause);
        }
    }

    private static void show(Object manager, boolean initialize)
            throws ReflectiveOperationException {
        try {
            manager.getClass()
                    .getMethod("showStatusBar", boolean.class)
                    .invoke(manager, initialize);
        } catch (InvocationTargetException failure) {
            Throwable cause = failure.getCause();

            if (cause instanceof RuntimeException) {
                throw (RuntimeException) cause;
            }

            if (cause instanceof Error) {
                throw (Error) cause;
            }

            throw new IllegalStateException("OEM status invocation failed", cause);
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
                    "PassionStatusReady",
                    "OEM status unavailable: real displays 0/1 not ready within 30s");
        }

        @Override
        public void showOriginal() {
            try {
                Class<?> type =
                        Class.forName(
                                "com.voyah.os.carsystemui.status.LYStatusBarManager",
                                true,
                                context.getClassLoader());
                Object manager = type.getMethod("getInstance", Context.class).invoke(null, context);

                if (manager == null) {
                    throw new IllegalStateException(
                            "OEM status factory returned null after real display readiness");
                }

                for (Object service : new ArrayList<Object>(REQUESTS.services.keySet())) {
                    Command command = REQUESTS.services.get(service);

                    if (command == null) {
                        continue;
                    }

                    serviceField(service).set(service, manager);

                    if (command.initialize) {
                        show(manager, true);
                        REQUESTS.initializations.remove(service);
                    }

                    if (!command.visible) {
                        invoke(manager, "removeStatusBar");
                    } else if (!command.initialize) {
                        show(manager, false);
                    }
                }
            } catch (InvocationTargetException failure) {
                Throwable cause = failure.getCause();

                if (cause instanceof RuntimeException) {
                    throw (RuntimeException) cause;
                }

                if (cause instanceof Error) {
                    throw (Error) cause;
                }

                throw new IllegalStateException("OEM status invocation failed", cause);
            } catch (ReflectiveOperationException failure) {
                throw new IllegalStateException("Pinned OEM status contract unavailable", failure);
            } finally {
                REQUESTS.clear();
            }
        }
    }
}
