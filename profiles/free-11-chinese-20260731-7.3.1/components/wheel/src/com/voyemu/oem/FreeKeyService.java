package com.voyemu.oem;

import android.app.Service;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;
import android.os.SystemClock;

import com.voyemu.input.FreeWheelBroker;
import com.voyemu.input.FreeWheelProtocol;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

/** Own service bound through the pinned Free API30 explicit package/action contract. */
public final class FreeKeyService extends Service {
    public static final FreeWheelBroker BROKER = new FreeWheelBroker();
    private static final ThreadLocal<Long> DEADLINE = new ThreadLocal<>();
    private static final ThreadPoolExecutor WORKERS =
            new ThreadPoolExecutor(
                    2,
                    2,
                    0,
                    TimeUnit.MILLISECONDS,
                    new ArrayBlockingQueue<Runnable>(32),
                    task -> {
                        Thread thread = new Thread(task, "voyemu-free-key-callback");
                        thread.setDaemon(true);

                        return thread;
                    });

    public static void beginDelivery() {
        DEADLINE.set(SystemClock.elapsedRealtime() + 1200);
    }

    public static void endDelivery() {
        DEADLINE.remove();
    }

    private final Map<String, Registration> registrations = new LinkedHashMap<>();

    private final class Registration implements IBinder.DeathRecipient {
        final IBinder token, callback;
        final String name;
        final int uid, type;

        Registration(IBinder token, IBinder callback, String name, int uid, int type) {
            this.token = token;
            this.callback = callback;
            this.name = name;
            this.uid = uid;
            this.type = type;
        }

        @Override
        public void binderDied() {
            remove(this);
        }

        void unlink() {
            try {
                token.unlinkToDeath(this, 0);
            } catch (java.util.NoSuchElementException absent) {
                // The token was already unlinked or died.
            }

            if (!token.equals(callback)) {
                try {
                    callback.unlinkToDeath(this, 0);
                } catch (java.util.NoSuchElementException absent) {
                    // The callback was already unlinked or died.
                }
            }
        }

        boolean deliver(int key, int action, String registration, int listenerType)
                throws Exception {
            Long deadline = DEADLINE.get();
            long remaining = deadline == null ? 0 : deadline - SystemClock.elapsedRealtime();

            if (remaining <= 0) {
                throw new RemoteException("wheel-delivery-budget");
            }

            Future<Boolean> result = null;

            try {
                result =
                        WORKERS.submit(() -> {
                            Parcel data = Parcel.obtain(), reply = Parcel.obtain();

                            try {
                                FreeWheelBroker.writeCallback(
                                        new FreeWheelBroker.Wire() {
                                            @Override
                                            public void token(String value) {
                                                data.writeInterfaceToken(value);
                                            }

                                            @Override
                                            public void number(int value) {
                                                data.writeInt(value);
                                            }

                                            @Override
                                            public void text(String value) {
                                                data.writeString(value);
                                            }

                                            @Override
                                            public void nullBundle() {
                                                data.writeBundle(null);
                                            }
                                        },
                                        key,
                                        action,
                                        registration,
                                        listenerType);

                                if (!callback.transact(2, data, reply, 0)) {
                                    throw new RemoteException("wheel-callback-unsupported");
                                }

                                reply.readException();
                                int handled = reply.readInt();

                                if (handled != 0 && handled != 1) {
                                    throw new RemoteException("wheel-callback-invalid");
                                }

                                if (reply.dataAvail() != 0) {
                                    throw new RemoteException("wheel-callback-trailing");
                                }

                                return handled == 1;
                            } finally {
                                reply.recycle();
                                data.recycle();
                            }
                        });

                return result.get(remaining, TimeUnit.MILLISECONDS);
            } catch (Exception failure) {
                // checkstyle-allow IllegalCatch: any callback wait failure cancels pending work and
                // removes the listener before rethrow
                if (result != null) {
                    result.cancel(true);
                }

                remove(this);
                throw failure;
            }
        }
    }

    private static String id(String name, int type) {
        return type + ":" + name;
    }

    private synchronized void remove(Registration value) {
        if (registrations.get(id(value.name, value.type)) != value) {
            return;
        }

        registrations.remove(id(value.name, value.type));
        BROKER.unregister(value.uid, value.name, value.type, value.callback);
        value.unlink();
    }

    private void checkPackage(String name) {
        if (!FreeWheelBroker.registrationAllowed(
                name, getPackageManager().getPackagesForUid(Binder.getCallingUid()))) {
            throw new SecurityException("wheel-registration-owner");
        }
    }

    private synchronized int register(
            IBinder token,
            IBinder callback,
            int[] keys,
            String name,
            int flags,
            int priority,
            int type) {
        checkPackage(name);

        if (token == null
                || callback == null
                || !token.isBinderAlive()
                || !callback.isBinderAlive()) {
            return -1;
        }

        Registration value = new Registration(token, callback, name, Binder.getCallingUid(), type);

        try {
            token.linkToDeath(value, 0);

            if (!token.equals(callback)) {
                callback.linkToDeath(value, 0);
            }
        } catch (RemoteException dead) {
            value.unlink();

            return -1;
        }

        int status =
                BROKER.register(
                        value.uid, name, type, priority, flags, keys, callback, value::deliver);

        if (status < 0) {
            value.unlink();

            return status;
        }

        Registration previous = registrations.put(id(name, type), value);

        if (previous != null) {
            previous.unlink();
        }

        return 0;
    }

    private final Binder binder =
            new Binder() {
                @Override
                protected boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                        throws RemoteException {
                    if (code == INTERFACE_TRANSACTION) {
                        reply.writeString(FreeWheelBroker.SERVICE);

                        return true;
                    }

                    if (!FreeWheelProtocol.supports(code)) {
                        return super.onTransact(code, data, reply, flags);
                    }

                    if (flags != 0 || reply == null) {
                        throw new IllegalArgumentException("wheel-sync-required");
                    }

                    int status =
                            FreeWheelProtocol.transact(
                                    code,
                                    flags,
                                    new FreeWheelProtocol.Input() {
                                        @Override
                                        public void enforce(String value) {
                                            data.enforceInterface(value);
                                        }

                                        @Override
                                        public Object binder() {
                                            return data.readStrongBinder();
                                        }

                                        @Override
                                        public int[] keys() {
                                            return data.createIntArray();
                                        }

                                        @Override
                                        public String text() {
                                            return data.readString();
                                        }

                                        @Override
                                        public int number() {
                                            return data.readInt();
                                        }

                                        @Override
                                        public int remaining() {
                                            return data.dataAvail();
                                        }
                                    },
                                    new FreeWheelProtocol.Operations() {
                                        @Override
                                        public int register(
                                                Object token,
                                                Object callback,
                                                int[] keys,
                                                String name,
                                                int clientFlags,
                                                int priority,
                                                int type) {
                                            return FreeKeyService.this.register(
                                                    (IBinder) token,
                                                    (IBinder) callback,
                                                    keys,
                                                    name,
                                                    clientFlags,
                                                    priority,
                                                    type);
                                        }

                                        @Override
                                        public int unregister(
                                                Object callback, String name, int type) {
                                            checkPackage(name);

                                            synchronized (FreeKeyService.this) {
                                                Registration value =
                                                        registrations.get(id(name, type));

                                                if (value != null
                                                        && value.uid == Binder.getCallingUid()
                                                        && value.callback.equals(callback)) {
                                                    remove(value);
                                                }
                                            }

                                            return 0;
                                        }
                                    });
                    reply.writeNoException();
                    reply.writeInt(status);

                    return true;
                }
            };

    @Override
    public IBinder onBind(Intent intent) {
        return intent != null
                        && "com.qinggan.keymanager.action.KEY_MANAGER_SERVICE"
                                .equals(intent.getAction())
                ? binder
                : null;
    }

    @Override
    public void onDestroy() {
        synchronized (this) {
            for (Registration value : new java.util.ArrayList<>(registrations.values())) {
                remove(value);
            }
        }

        super.onDestroy();
    }
}
