package com.voyemu.display;

import android.view.SurfaceControl.Transaction;

import com.android.server.display.DisplayManagerService;

import com.mega.nexus.server.ScreenStateProcessor;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Executes the actual guest helpers against controlled platform boundaries. */
public final class PassionPowerHelperTest {
    private static int checks;

    private static void check(boolean ok, String message) {
        checks++;

        if (!ok) {
            throw new AssertionError(message);
        }
    }

    private static void reject(Runnable call) {
        boolean rejected = false;

        try {
            call.run();
        } catch (IllegalArgumentException | IllegalStateException | SecurityException expected) {
            rejected = true;
        }

        check(rejected, "invalid power request accepted");
    }

    private static void backend() throws Exception {
        DisplayManagerService service = new DisplayManagerService();
        DmsDisplayPowerBackend backend = new DmsDisplayPowerBackend(service);

        try (PassionDisplayPower power = new PassionDisplayPower(backend)) {
            reject(() -> power.request(2000, 2, 1));
            reject(() -> power.request(0, 1, 1));
            reject(() -> power.request(0, 2, 0));
            reject(() -> power.request(0, 2, 1, 0));
            PassionDisplayPower.Proof off = power.request(0, 2, 1);
            Object device = backend.deviceObject(0);
            check(off.applied() && off.state() == 1 && off.transaction() > 0, "local OFF proof");
            check(
                    DmsDisplayPowerBackend.effectiveState(device, 2) == 1,
                    "committed local override");
            check(!DmsDisplayPowerBackend.pending(device), "committed request still pending");
            check(power.request(1000, 3, 1).state() == 1, "virtual OFF path");
            check(power.request(0, 3, 2).state() == 2, "virtual ON path");
            int closed = Transaction.closed;
            Transaction.failApply = true;
            reject(() -> power.request(0, 2, 2));
            Transaction.failApply = false;
            check(Transaction.closed >= closed + 2, "failed apply and rollback must close");
            check(!DmsDisplayPowerBackend.pending(device), "failed transaction leaked pending");
            check(power.effective(power.target(0), 2) == 1, "failure published desired state");
            check(power.request(0, 2, 2).state() == 2, "next operation after failed apply");
            closed = Transaction.closed;
            Transaction.failClose = true;
            reject(() -> power.request(0, 2, 1));
            Transaction.failClose = false;
            check(Transaction.closed >= closed + 2, "close failure skipped rollback close");
            check(!DmsDisplayPowerBackend.pending(device), "close failure leaked pending");
            check(power.request(0, 2, 1).state() == 1, "recovery after failed close");
            PassionDisplayPower.Device old = power.target(0);
            service.replacePrimary();
            check(!DmsDisplayPowerBackend.pending(backend.deviceObject(0)), "replacement pending");
            check(
                    DmsDisplayPowerBackend.effectiveState(backend.deviceObject(0), 2) == 2,
                    "replacement inherited committed override");
            check(power.target(0).equals(old), "replacement retains canonical public identity");
        }
    }

    public static final class Shell {
        String[] seen;
        boolean fail;

        public int exec(
                android.os.Binder binder,
                java.io.FileDescriptor in,
                java.io.FileDescriptor out,
                java.io.FileDescriptor err,
                String[] args,
                android.os.ShellCallback callback,
                android.os.ResultReceiver receiver) {
            if (fail) {
                throw new IllegalArgumentException("original shell failure");
            }

            seen = args;

            return 37;
        }
    }

    private static void shell() {
        Shell shell = new Shell();
        String[] args = {"original-command", "unchanged"};

        check(
                DmsPowerCommand.exec(shell, null, null, null, null, args, null, null) == 37,
                "original shell status changed");
        check(shell.seen == args, "original argument object replaced");
        check(
                DmsPowerCommand.exec(shell, null, null, null, null, null, null, null) == 37,
                "null original arguments not delegated");
        shell.fail = true;
        reject(() -> DmsPowerCommand.exec(shell, null, null, null, null, args, null, null));
    }

    private static void dispatch(Object bridge, String... args) {
        try {
            Class<?> shellType = Class.forName(DmsPowerCommand.class.getName() + "$ShellCall");
            java.lang.reflect.Constructor<?> constructor = shellType.getDeclaredConstructors()[0];
            constructor.setAccessible(true);
            Object shell = constructor.newInstance(null, null, null, null, args, null, null);
            java.lang.reflect.Method dispatch =
                    DmsPowerCommand.class.getDeclaredMethod(
                            "dispatch", bridge.getClass(), int.class, String.class, shellType);
            dispatch.setAccessible(true);
            dispatch.invoke(null, bridge, 0, "same-boot", shell);
        } catch (java.lang.reflect.InvocationTargetException invocation) {
            Throwable cause = invocation.getCause();

            if (cause instanceof RuntimeException) {
                throw (RuntimeException) cause;
            }

            throw new AssertionError(cause);
        } catch (ReflectiveOperationException unavailable) {
            throw new AssertionError(unavailable);
        }
    }

    private static void commands() throws Exception {
        Class<?> bridgeType = Class.forName(DmsPowerCommand.class.getName() + "$Bridge");
        java.lang.reflect.Constructor<?> constructor =
                bridgeType.getDeclaredConstructor(Object.class);

        constructor.setAccessible(true);
        Object bridge = constructor.newInstance(new DisplayManagerService());
        PassionDisplayPower power =
                (PassionDisplayPower) DmsDisplayPowerBackend.field(bridge, "power");

        try {
            dispatch(bridge, "voyemu-passion-screen", "query", "0");
            dispatch(bridge, "voyemu-passion-screen", "set", "0", "OFF");
            check(power.effective(power.target(0), 2) == 1, "reserved OFF command not applied");
            dispatch(bridge, "voyemu-passion-screen", "set", "1", "ON");
            check(power.effective(power.target(1), 1) == 2, "reserved secondary ON not applied");
            reject(() -> dispatch(bridge, "reserved", "query", "2"));
            reject(() -> dispatch(bridge, "reserved", "query", "0", "extra"));
            reject(() -> dispatch(bridge, "reserved", "set", "0", "off"));
            reject(() -> dispatch(bridge, "reserved", "unknown", "0"));
            reject(() -> dispatch(bridge, "reserved", "watch", "invalid", "invalid", "same-boot"));
            String id = "0123456789abcdef0123456789abcdef";
            reject(() -> dispatch(bridge, "reserved", "watch", id, id, "foreign-boot"));
            dispatch(bridge, "reserved", "query", "0");
            check(power.effective(power.target(0), 2) == 1, "query changed committed state");
        } finally {
            power.close();
        }
    }

    private static final class BlockingBackend implements PassionDisplayPower.Backend {
        final CountDownLatch entered = new CountDownLatch(1), release = new CountDownLatch(1);
        final CountDownLatch rolledBack = new CountDownLatch(1);
        volatile int commits;

        @Override
        public PassionDisplayPower.Device device(int logical) {
            return new PassionDisplayPower.Device(logical, "LOCAL", "local:8141241408256768");
        }

        @Override
        public PassionDisplayPower.Proof apply(
                PassionDisplayPower.Device device, PassionDisplayPower.Request request, int state) {
            entered.countDown();

            try {
                if (!release.await(5, TimeUnit.SECONDS)) {
                    throw new AssertionError("test release timeout");
                }
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException(interrupted);
            }

            return new PassionDisplayPower.Proof(
                    device.uniqueId(),
                    request.sequence,
                    1,
                    state,
                    true,
                    System.currentTimeMillis());
        }

        @Override
        public PassionDisplayPower.Proof observe(
                PassionDisplayPower.Device device, PassionDisplayPower.Request request) {
            return new PassionDisplayPower.Proof(
                    device.uniqueId(), request.sequence, 1, 1, true, System.currentTimeMillis());
        }

        @Override
        public void commit(PassionDisplayPower.Device device, PassionDisplayPower.Request request) {
            commits++;
        }

        @Override
        public void rollback(
                PassionDisplayPower.Device device, PassionDisplayPower.Request request) {
            rolledBack.countDown();
        }
    }

    private static void timeout() throws Exception {
        BlockingBackend backend = new BlockingBackend();

        try (PassionDisplayPower power = new PassionDisplayPower(backend)) {
            reject(() -> power.request(0, 2, 1, 100));
            check(backend.entered.await(1, TimeUnit.SECONDS), "timeout did not enter backend");
            backend.release.countDown();
            check(
                    backend.rolledBack.await(2, TimeUnit.SECONDS),
                    "timed-out work did not roll back");
            check(backend.commits == 0, "late completion committed after timeout");
            check(power.effective(backend.device(0), 2) == 2, "timeout published desired state");
        } finally {
            backend.release.countDown();
        }
    }

    private static final class Platform implements OemDisplayPower.Platform {
        boolean fail;
        int sets;

        @Override
        public String boot() {
            return "same-boot";
        }

        OemDisplayPower.Reply reply(int display, boolean on, boolean applied) {
            return new OemDisplayPower.Reply(
                    "same-boot",
                    display,
                    OemDisplayPower.identity(display),
                    on ? 2 : 1,
                    applied ? "applied" : "observed",
                    new OemDisplayPower.Observation(1, 1, 1, 1));
        }

        @Override
        public OemDisplayPower.Reply query(int display) {
            return reply(display, true, false);
        }

        @Override
        public OemDisplayPower.Reply set(int display, boolean on) {
            sets++;

            if (fail) {
                throw new IllegalStateException("injected OEM send failure");
            }

            return reply(display, on, true);
        }
    }

    public static final class Listener {
        int result = -1;

        public void onResult(int value) {
            result = value;
        }
    }

    private static void oem() throws Exception {
        Platform platform = new Platform();
        OemDisplayPower.Engine engine = new OemDisplayPower.Engine(platform);
        ScreenStateProcessor processor = new ScreenStateProcessor();

        processor.mOnStateCtrlList.push("on");
        processor.mOffStateCtrlList.push("off");
        engine.before(processor);
        check(engine.send(processor, false), "canonical OEM send not intercepted");
        check(!processor.mScreenOnTarget && processor.reversals == 2, "applied state not reversed");
        Listener listener = new Listener();
        engine.result(processor, listener, 0);
        check(listener.result == 0, "successful OEM result changed");
        platform.fail = true;
        processor.mScreenOnTarget = true;
        engine.before(processor);
        processor.mOnStateCtrlList.clear();
        processor.mOffStateCtrlList.clear();
        check(engine.send(processor, false), "failed canonical send fell through");
        engine.result(processor, listener, 0);
        check(listener.result == 2 && processor.mScreenOnTarget, "failed send claimed success");
        check(
                processor.mOnStateCtrlList.peek().equals("on")
                        && processor.mOffStateCtrlList.peek().equals("off"),
                "OEM policy not restored");
        processor.mScreenId = 4;
        int sets = platform.sets;
        check(
                !engine.send(processor, false) && platform.sets == sets,
                "foreign display intercepted");
        reject(() ->
                OemDisplayPower.validate(
                        platform.reply(0, true, true), "foreign-boot", 0, true, true));
        reject(() ->
                OemDisplayPower.validate(
                        platform.reply(1, true, true), "same-boot", 0, true, true));
        reject(() ->
                OemDisplayPower.validate(
                        platform.reply(0, false, true), "same-boot", 0, true, true));
    }

    public static void main(String[] args) throws Exception {
        backend();
        shell();
        commands();
        timeout();
        oem();
        System.out.println("PASS real Passion helpers: " + checks + " behavior assertions");
    }
}
