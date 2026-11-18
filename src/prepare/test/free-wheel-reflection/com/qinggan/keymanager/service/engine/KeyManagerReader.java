package com.qinggan.keymanager.service.engine;

import com.qinggan.system.QGKeyEvent;

import java.util.HashMap;
import java.util.Map;

/** Own behavioral fixture: inaccessible classes and callback lock assertions. */
public final class KeyManagerReader {
    private static final class Processor {
        private final Map<Integer, KeyProcess> mProcesses = new HashMap<>();
        int removals;

        public void removeMessages(int code) {
            removals++;
        }

        public void keyOut(QGKeyEvent event) {
            removeMessages(event.getKeyCode());
            KeyProcess process;

            synchronized (mProcesses) {
                process = mProcesses.remove(event.getKeyCode());
            }

            if (process != null) {
                process.setPhase(8);
            }
        }
    }

    private static final class KeyProcess {
        private int phase;
        final Processor owner;
        int observed = -1;
        boolean fail;

        KeyProcess(Processor owner, int phase) {
            this.owner = owner;
            this.phase = phase;
        }

        public synchronized void setPhase(int next) {
            if (Thread.holdsLock(owner.mProcesses)) {
                throw new AssertionError("map held across callback");
            }

            Thread callback =
                    new Thread(() -> {
                        synchronized (owner.mProcesses) {
                            // checkstyle-allow EmptyBlock: acquiring the monitor is the probe
                        }
                    });
            callback.start();

            try {
                callback.join(200);
            } catch (InterruptedException error) {
                throw new AssertionError(error);
            }

            if (callback.isAlive()) {
                throw new AssertionError("callback cannot reenter map");
            }

            observed = phase;
            phase = next;

            if (fail) {
                throw new IllegalArgumentException("original-release-error");
            }
        }
    }

    public static void main(String[] args) {
        for (int phase : new int[] {1, 2, 4}) {
            Processor normal = new Processor();
            KeyProcess a = new KeyProcess(normal, phase);
            normal.mProcesses.put(173, a);
            com.voyemu.input.FreeWheelCancellation.keyOut(normal, new QGKeyEvent(173, false));

            if (a.observed != phase || normal.removals != 1 || !normal.mProcesses.isEmpty()) {
                throw new AssertionError("ordinary UP changed");
            }

            Processor cancel = new Processor();
            KeyProcess b = new KeyProcess(cancel, phase);
            cancel.mProcesses.put(173, b);
            com.voyemu.input.FreeWheelCancellation.keyOut(cancel, new QGKeyEvent(173, true));

            if (b.observed != (phase == 1 ? 0 : phase)
                    || cancel.removals != 1
                    || !cancel.mProcesses.isEmpty()) {
                throw new AssertionError("cancel changed");
            }
        }

        Processor empty = new Processor();
        com.voyemu.input.FreeWheelCancellation.keyOut(empty, new QGKeyEvent(173, true));

        if (empty.removals != 1) {
            throw new AssertionError("empty release lost timer removal");
        }

        Processor failed = new Processor();
        KeyProcess f = new KeyProcess(failed, 1);
        f.fail = true;
        failed.mProcesses.put(173, f);

        try {
            com.voyemu.input.FreeWheelCancellation.keyOut(failed, new QGKeyEvent(173, true));
            throw new AssertionError("lost original exception");
        } catch (IllegalArgumentException expected) {
            if (!"original-release-error".equals(expected.getMessage())) {
                throw expected;
            }
        }

        System.out.println(
                "Package-private reflection; ordinary/cancel phases; callbacks outside map;"
                        + " original errors PASS");
    }
}
