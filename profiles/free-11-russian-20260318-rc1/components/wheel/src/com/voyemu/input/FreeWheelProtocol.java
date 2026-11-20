package com.voyemu.input;

/** Finite Free API30 registration decoder; unsupported OEM operations are not acknowledged. */
public final class FreeWheelProtocol {
    public interface Input {
        void enforce(String descriptor);

        Object binder();

        int[] keys();

        String text();

        int number();

        int remaining();
    }

    public interface Operations {
        int register(
                Object token,
                Object callback,
                int[] keys,
                String name,
                int flags,
                int priority,
                int type);

        int unregister(Object callback, String name, int type);
    }

    public static boolean supports(int code) {
        return code >= 1 && code <= 4;
    }

    public static int transact(int code, int flags, Input input, Operations operations) {
        if (!supports(code)) {
            throw new UnsupportedOperationException("wheel-operation-unsupported");
        }

        if (flags != 0) {
            throw new IllegalArgumentException("wheel-sync-required");
        }

        input.enforce(FreeWheelBroker.SERVICE);
        int type = code <= 2 ? 0 : 1;

        if (code == 1 || code == 3) {
            Object token = input.binder(), callback = input.binder();
            int[] keys = input.keys();
            String name = input.text();
            int clientFlags = input.number(), priority = input.number();

            if (input.remaining() != 0) {
                throw new IllegalArgumentException("wheel-trailing-data");
            }

            return operations.register(token, callback, keys, name, clientFlags, priority, type);
        }

        Object callback = input.binder();
        String name = input.text();

        if (input.remaining() != 0) {
            throw new IllegalArgumentException("wheel-trailing-data");
        }

        return operations.unregister(callback, name, type);
    }

    private FreeWheelProtocol() {}
}
