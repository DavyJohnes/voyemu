package com.voyemu.input;

import android.view.KeyEvent;

import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.Map;

/** Exact pinned OEM reader boundary: cancel releases without manufacturing a short click. */
public final class FreeWheelCancellation {
    public static int cancelPhase(int phase) {
        return phase == 1 ? 0 : phase;
    }

    public static void keyOut(Object processor, KeyEvent event) {
        if (processor == null
                || event == null
                || !processor
                        .getClass()
                        .getName()
                        .equals("com.qinggan.keymanager.service.engine.KeyManagerReader$Processor")
                || !event.getClass().getName().equals("com.qinggan.system.QGKeyEvent")) {
            throw new IllegalArgumentException("free-key-cancel-owner");
        }

        try {
            Method release = processor.getClass().getDeclaredMethod("keyOut", event.getClass());
            release.setAccessible(true);

            if (!event.isCanceled()) {
                release.invoke(processor, event);

                return;
            }

            // Exact pinned cancellation boundary: retire timer and detach under the map lock,
            // then release that lock before original phase callbacks (as ordinary keyOut does).
            Method removeMessages = processor.getClass().getMethod("removeMessages", int.class);
            removeMessages.setAccessible(true);
            removeMessages.invoke(processor, event.getKeyCode());
            Field processes = processor.getClass().getDeclaredField("mProcesses");
            processes.setAccessible(true);
            Object value = processes.get(processor);

            if (!(value instanceof Map)) {
                throw new IllegalStateException("free-key-cancel-map");
            }

            Object process;

            synchronized (value) {
                process = ((Map<?, ?>) value).remove(event.getKeyCode());
            }

            if (process == null) {
                return;
            }

            if (!process.getClass()
                    .getName()
                    .equals("com.qinggan.keymanager.service.engine.KeyManagerReader$KeyProcess")) {
                throw new IllegalStateException("free-key-cancel-process");
            }

            synchronized (process) {
                Field phase = process.getClass().getDeclaredField("phase");
                phase.setAccessible(true);
                int before = phase.getInt(process);

                if (before == 1) {
                    phase.setInt(process, cancelPhase(before));
                }

                // Serialize with the original timer/phase monitor; HOLD/LONG stop actions stay OEM.
                Method setPhase = process.getClass().getDeclaredMethod("setPhase", int.class);
                setPhase.setAccessible(true);
                setPhase.invoke(process, 8);
            }
        } catch (InvocationTargetException error) {
            Throwable cause = error.getCause();

            if (cause instanceof RuntimeException) {
                throw (RuntimeException) cause;
            }

            if (cause instanceof Error) {
                throw (Error) cause;
            }

            throw new IllegalStateException("free-key-cancel-release", cause);
        } catch (ReflectiveOperationException error) {
            throw new IllegalStateException("free-key-cancel-contract", error);
        }
    }

    private FreeWheelCancellation() {}
}
