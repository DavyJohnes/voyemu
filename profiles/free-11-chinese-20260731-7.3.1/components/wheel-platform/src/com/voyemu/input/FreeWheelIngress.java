package com.voyemu.input;

import android.app.ActivityManager;
import android.content.Context;
import android.content.pm.PackageManager;
import android.content.pm.ProviderInfo;
import android.os.Binder;
import android.os.Process;
import android.util.Log;
import android.view.KeyEvent;

import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.util.List;

/** One authenticated provider ingress; the original reader owns phases and timers. */
public final class FreeWheelIngress {
    private static final String PACKAGE = "com.voyemu.input.wheel",
            SERVICE = "com.qinggan.keymanager.service";
    private static final String ENGINE = SERVICE + ".engine.KeyManagerEngine",
            MESSAGE = SERVICE + ".engine.KeyManagerMessage";
    private static final String READER = SERVICE + ".engine.KeyManagerReader",
            EVENT = "com.qinggan.system.QGKeyEvent";

    private static boolean ownProvider(Context context, int uid, int pid) {
        if (uid != 1000
                || pid <= 0
                || pid == Process.myPid()
                || !SERVICE.equals(context.getPackageName())) {
            return false;
        }

        PackageManager packages = context.getPackageManager();
        ProviderInfo provider = packages.resolveContentProvider(PACKAGE, 0);

        if (!providerIdentity(provider, packages, uid)) {
            return false;
        }

        ActivityManager manager =
                (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        List<ActivityManager.RunningAppProcessInfo> processes =
                manager == null ? null : manager.getRunningAppProcesses();

        if (processes == null || processes.size() > 256) {
            return false;
        }

        int found = 0;

        for (ActivityManager.RunningAppProcessInfo process : processes) {
            if (process.pid == pid) {
                if (process.uid != uid
                        || !PACKAGE.equals(process.processName)
                        || process.pkgList == null
                        || process.pkgList.length != 1
                        || !PACKAGE.equals(process.pkgList[0])) {
                    return false;
                }

                found++;
            }
        }

        return found == 1;
    }

    private static boolean providerIdentity(
            ProviderInfo provider, PackageManager packages, int uid) {
        return !(provider == null
                || provider.applicationInfo == null
                || !provider.enabled
                || !provider.exported
                || !provider.applicationInfo.enabled
                || !PACKAGE.equals(provider.authority)
                || !PACKAGE.equals(provider.packageName)
                || !"com.voyemu.input.FreeKeyInputProvider".equals(provider.name)
                || !"android.permission.DUMP".equals(provider.readPermission)
                || !"android.permission.DUMP".equals(provider.writePermission)
                || provider.applicationInfo.uid != uid
                || !PACKAGE.equals(provider.applicationInfo.packageName)
                || !PACKAGE.equals(provider.processName)
                || !PACKAGE.equals(provider.applicationInfo.processName)
                || packages.checkSignatures(SERVICE, PACKAGE) != PackageManager.SIGNATURE_MATCH);
    }

    private static boolean invoke(Object owner, String name, Class<?> argument, Object value)
            throws ReflectiveOperationException {
        try {
            Object result = owner.getClass().getMethod(name, argument).invoke(owner, value);

            if (!(result instanceof Boolean)) {
                throw new IllegalStateException("free-key-ingress-return");
            }

            return (Boolean) result;
        } catch (InvocationTargetException failure) {
            Throwable cause = failure.getCause();

            if (cause instanceof RuntimeException) {
                throw (RuntimeException) cause;
            }

            if (cause instanceof Error) {
                throw (Error) cause;
            }

            throw new IllegalStateException("free-key-ingress-original", cause);
        }
    }

    private static KeyEvent validatedEvent(Object message) throws ReflectiveOperationException {
        if (message.getClass().getField("mWhat").getInt(message) != 15) {
            throw new IllegalArgumentException("free-key-ingress-message");
        }

        Object value = message.getClass().getField("keyEvent").get(message);

        if (!(value instanceof KeyEvent) || !EVENT.equals(value.getClass().getName())) {
            throw new IllegalArgumentException("free-key-ingress-event");
        }

        KeyEvent event = (KeyEvent) value;

        switch (event.getKeyCode()) {
            case 1:
            case 2:
            case 3:
            case 4:
            case 6:
            case 128:
            case 130:
            case 173:
            case 3074:
            case 3090:
                break;
            default:
                throw new IllegalArgumentException("free-key-ingress-key");
        }

        if (!((event.getAction() == 4096 && event.getFlags() == 0)
                || (event.getAction() == 65536
                        && (event.getFlags() == 0 || event.getFlags() == 32)))) {
            throw new IllegalArgumentException("free-key-ingress-phase");
        }

        return event;
    }

    // Compares identity on purpose: the same Binder, token or instance, never an equal one.
    @SuppressWarnings("ReferenceEquality")
    private static Object originalReader(Object engine) throws ReflectiveOperationException {
        Field modules = engine.getClass().getDeclaredField("mModules");

        if (modules.getType() != List.class) {
            throw new IllegalStateException("free-key-ingress-modules");
        }

        modules.setAccessible(true);
        Object raw = modules.get(engine);

        if (!(raw instanceof List) || ((List<?>) raw).isEmpty() || ((List<?>) raw).size() > 16) {
            throw new IllegalStateException("free-key-ingress-modules");
        }

        Object reader = null;

        for (Object module : (List<?>) raw) {
            if (module != null && READER.equals(module.getClass().getName())) {
                if (reader != null
                        || module.getClass().getClassLoader()
                                != engine.getClass().getClassLoader()) {
                    throw new IllegalStateException("free-key-ingress-reader");
                }

                reader = module;
            }
        }

        if (reader == null) {
            throw new IllegalStateException("free-key-ingress-reader");
        }

        return reader;
    }

    public static boolean route(Object engine, Object message) {
        // Capture the kernel Binder caller before reflection, outbound IPC or asynchronous
        // dispatch.
        final int uid = Binder.getCallingUid(), pid = Binder.getCallingPid();

        if (engine == null
                || message == null
                || !ENGINE.equals(engine.getClass().getName())
                || !MESSAGE.equals(message.getClass().getName())) {
            throw new IllegalArgumentException("free-key-ingress-owner");
        }

        try {
            String caller =
                    (String) message.getClass().getField("mCallingPackageName").get(message);

            if (!PACKAGE.equals(caller)) {
                return invoke(engine, "pushMsg", message.getClass(), message);
            }

            Context context = (Context) engine.getClass().getMethod("getContext").invoke(engine);
            boolean authenticated;

            try {
                authenticated = ownProvider(context, uid, pid);
            } catch (RuntimeException unavailable) {
                // checkstyle-allow IllegalCatch: unavailable Android identity checks cannot
                // authenticate ingress
                authenticated = false;
            }

            if (!authenticated) {
                return invoke(engine, "pushMsg", message.getClass(), message);
            }

            KeyEvent event = validatedEvent(message);
            Object reader = originalReader(engine);
            Log.i(
                    "VoyemuWheel",
                    "ingress original-reader key="
                            + event.getKeyCode()
                            + " action="
                            + event.getAction()
                            + " flags="
                            + event.getFlags());

            return invoke(reader, "onKeyEvent", KeyEvent.class, event);
        } catch (ReflectiveOperationException failure) {
            throw new IllegalStateException("free-key-ingress-contract", failure);
        }
    }

    private FreeWheelIngress() {}
}
