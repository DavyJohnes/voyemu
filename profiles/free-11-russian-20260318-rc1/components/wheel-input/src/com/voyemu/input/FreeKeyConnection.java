package com.voyemu.input;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.content.pm.ServiceInfo;
import android.os.IBinder;
import android.os.Parcel;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Only the genuine, platform-signed Free KeyManager component can receive these requests. */
final class FreeKeyConnection {
    static final String PACKAGE = "com.qinggan.keymanager.service";
    static final String CLASS = PACKAGE + ".KeyManagerService";
    static final String DESCRIPTOR = "com.qinggan.system.IKeyManagerService";
    private final Context context;
    private final Object lock = new Object();
    private final FreeKeySubmission submissions = new FreeKeySubmission(1000);
    private volatile IBinder binder;
    private boolean binding;
    private CountDownLatch ready = new CountDownLatch(1);
    private final ServiceConnection connection =
            new ServiceConnection() {
                @Override
                public void onServiceConnected(ComponentName name, IBinder value) {
                    synchronized (lock) {
                        try {
                            if (!name.equals(new ComponentName(PACKAGE, CLASS))
                                    || value == null
                                    || !value.isBinderAlive()
                                    || !DESCRIPTOR.equals(value.getInterfaceDescriptor())) {
                                throw new SecurityException("free-key-bound-owner");
                            }

                            binder = value;
                        } catch (Exception refused) {
                            // checkstyle-allow IllegalCatch: any rejected Binder owner clears the
                            // connection before waking waiters
                            binder = null;
                        }

                        ready.countDown();
                    }
                }

                @Override
                public void onServiceDisconnected(ComponentName name) {
                    synchronized (lock) {
                        binder = null;
                        ready = new CountDownLatch(1);
                    }
                }

                @Override
                public void onBindingDied(ComponentName name) {
                    synchronized (lock) {
                        binder = null;
                        binding = false;
                        ready.countDown();
                    }

                    context.unbindService(this);
                }

                @Override
                public void onNullBinding(ComponentName name) {
                    synchronized (lock) {
                        binder = null;
                        binding = false;
                        ready.countDown();
                    }

                    context.unbindService(this);
                }
            };

    FreeKeyConnection(Context context) {
        this.context = context.getApplicationContext();
    }

    void ensure() throws Exception {
        IBinder current = binder;

        if (current != null && current.isBinderAlive()) {
            return;
        }

        PackageManager packages = context.getPackageManager();
        ComponentName component = new ComponentName(PACKAGE, CLASS);
        ServiceInfo service = packages.getServiceInfo(component, 0);

        if (!service.enabled
                || !service.exported
                || service.applicationInfo.uid != 1000
                || packages.checkSignatures(context.getPackageName(), PACKAGE)
                        != PackageManager.SIGNATURE_MATCH) {
            throw new SecurityException("free-key-installed-owner");
        }

        boolean start = false;
        CountDownLatch pending;

        synchronized (lock) {
            if (!binding) {
                binding = true;
                ready = new CountDownLatch(1);
                start = true;
            }

            pending = ready;
        }

        if (start) {
            boolean accepted;

            try {
                accepted =
                        context.bindService(
                                new Intent("com.qinggan.keymanager.action.KEY_MANAGER_SERVICE")
                                        .setComponent(component)
                                        .addCategory("android.intent.category.QGKEYMANAGER"),
                                connection,
                                Context.BIND_AUTO_CREATE);
            } catch (RuntimeException failure) {
                // checkstyle-allow IllegalCatch: any platform bind failure resets waiters before
                // the same failure is rethrown
                synchronized (lock) {
                    binding = false;
                    ready.countDown();
                }

                throw failure;
            }

            if (!accepted) {
                synchronized (lock) {
                    binding = false;
                    ready.countDown();
                }

                throw new IllegalStateException("free-key-bind-refused");
            }
        }

        if (!pending.await(750, TimeUnit.MILLISECONDS)
                || binder == null
                || !binder.isBinderAlive()) {
            throw new IllegalStateException("free-key-unavailable");
        }
    }

    synchronized void submit(FreeKeyInputSession.Event event) throws Exception {
        final IBinder target = binder;

        if (target == null || !target.isBinderAlive()) {
            throw new IllegalStateException("free-key-disconnected");
        }

        submissions.submit(() -> {
            Parcel data = Parcel.obtain(), reply = Parcel.obtain();

            try {
                FreeKeyInputSession.writeEvent(
                        new FreeKeyInputSession.Wire() {
                            @Override
                            public void token(String value) {
                                data.writeInterfaceToken(value);
                            }

                            @Override
                            public void number(int value) {
                                data.writeInt(value);
                            }

                            @Override
                            public void nullBundle() {
                                data.writeBundle(null);
                            }

                            @Override
                            public void text(String value) {
                                data.writeString(value);
                            }
                        },
                        event);

                if (!target.transact(7, data, reply, 0)) {
                    throw new IllegalStateException("free-key-operation-unsupported");
                }

                reply.readException();

                if (reply.dataAvail() != 0) {
                    throw new IllegalStateException("free-key-trailing-reply");
                }

                return null;
            } finally {
                reply.recycle();
                data.recycle();
            }
        });
    }
}
