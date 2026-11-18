package android.os;

import java.lang.reflect.InvocationTargetException;

public final class PeerTestRuntime {
    public static final class Identity {
        public final int uid, pid;
        public final String name;

        public Identity(int uid, int pid, String name) {
            this.uid = uid;
            this.pid = pid;
            this.name = name;
        }
    }

    public static final Identity PHONE = new Identity(1000, 4101, "com.qinggan.bluetoothphone"),
            LAUNCHER = new Identity(1000, 4102, "com.qinggan.app.launcher");
    public static final InheritableThreadLocal<Identity> CURRENT =
            new InheritableThreadLocal<Identity>() {
                @Override
                protected Identity initialValue() {
                    return PHONE;
                }
            };
    public static final ThreadLocal<Identity> CALLER = new ThreadLocal<Identity>();
    public static final Object MAIN_LOCK = new Object();
    public static volatile String earlyMutation;
    public static volatile java.util.concurrent.CountDownLatch earlyDelivered;
    public static volatile Object phone;
    public static volatile IBinder callback, authority;
    public static volatile String lastSnapshot;

    public static void enter(String role) {
        CURRENT.set("phone".equals(role) ? PHONE : LAUNCHER);
    }

    public static void install(Object value) {
        phone = value;
    }

    public static Bundle call(String method, Bundle args) {
        Identity source = CURRENT.get(), oldCaller = CALLER.get();

        CURRENT.set(PHONE);
        CALLER.set(source);

        try {
            if ("subscribe".equals(method)) {
                callback = args.getBinder("callback");
                Bundle reply;

                synchronized (MAIN_LOCK) {
                    reply =
                            (Bundle)
                                    phone.getClass()
                                            .getMethod(
                                                    "subscribePeer",
                                                    IBinder.class,
                                                    int.class,
                                                    int.class)
                                            .invoke(phone, callback, source.uid, source.pid);
                }

                authority = reply.getBinder("authority");
                lastSnapshot = reply.getString("snapshot");

                if (earlyMutation != null) {
                    String mutation = earlyMutation;
                    earlyMutation = null;
                    earlyDelivered = new java.util.concurrent.CountDownLatch(1);
                    phone.getClass()
                            .getMethod("control", String.class, String.class)
                            .invoke(phone, mutation, "{}");

                    if (!earlyDelivered.await(2, java.util.concurrent.TimeUnit.SECONDS)) {
                        throw new AssertionError("early callback never delivered");
                    }
                }

                return reply;
            }

            if ("unsubscribe".equals(method)) {
                phone.getClass()
                        .getMethod("unsubscribePeer", IBinder.class, int.class, int.class)
                        .invoke(phone, args.getBinder("callback"), source.uid, source.pid);

                return new Bundle();
            }

            throw new SecurityException("fixture method");
        } catch (InvocationTargetException e) {
            if (e.getCause() instanceof RuntimeException) {
                throw (RuntimeException) e.getCause();
            }

            throw new IllegalStateException(e.getCause());
        } catch (Exception e) {
            throw new IllegalStateException(e);
        } finally {
            CURRENT.set(source);
            CALLER.set(oldCaller);
        }
    }

    public static void send(
            String snapshot, int uid, int pid, String token, int code, boolean trailing)
            throws Exception {
        sendFlags(snapshot, uid, pid, token, code, trailing, IBinder.FLAG_ONEWAY);
    }

    public static void sendFlags(
            String snapshot, int uid, int pid, String token, int code, boolean trailing, int flags)
            throws Exception {
        Identity old = CURRENT.get();

        CURRENT.set(new Identity(uid, pid, "sender"));

        try {
            Parcel p = Parcel.obtain();
            p.writeInterfaceToken(token);
            p.writeString(snapshot);

            if (trailing) {
                p.writeInt(1);
            }

            callback.transact(code, p, null, flags);
        } finally {
            CURRENT.set(old);
        }
    }

    public static void killAuthority() {
        ((Binder) authority).kill();
    }

    public static void killCallback() {
        ((Binder) callback).kill();
    }
}
