package com.voyemu.passion.phone;

import android.telecom.PhoneAccountHandle;
import android.telecom.TelecomManager;

public final class PassionPhoneAccountTest {
    interface Checked {
        void run() throws Exception;
    }

    static void reject(Checked value) throws Exception {
        try {
            value.run();
            throw new AssertionError("accepted");
        } catch (ReflectiveOperationException | IllegalStateException expected) {
        }
    }

    private static void oemDialer(TelecomManager manager) throws Exception {
        PassionPhoneAccount.requireOemDialer(manager);

        if (manager.dialerAttempts != 1
                || !"com.voyah.btphone".equals(manager.requestedDialer)
                || !manager.requestedDialer.equals(manager.dialer)) {
            throw new AssertionError();
        }

        PassionPhoneAccount.requireOemDialer(manager);

        if (manager.dialerAttempts != 1) {
            throw new AssertionError();
        }

        manager.dialer = "com.android.dialer";
        manager.dialerRefused = true;
        reject(() -> PassionPhoneAccount.requireOemDialer(manager));
        manager.dialerRefused = false;
        manager.dialerDenied = true;
        reject(() -> PassionPhoneAccount.requireOemDialer(manager));
        manager.dialerDenied = false;
        manager.dialerUnchanged = true;
        reject(() -> PassionPhoneAccount.requireOemDialer(manager));
    }

    public static void main(String[] args) throws Exception {
        PhoneAccountHandle handle = new PhoneAccountHandle();
        TelecomManager manager = new TelecomManager();

        if (!PassionPhoneAccount.accepts(handle, null, handle)
                || !PassionPhoneAccount.accepts(handle, handle, handle)) {
            throw new AssertionError();
        }

        if (PassionPhoneAccount.accepts(handle, new PhoneAccountHandle(), handle)
                || PassionPhoneAccount.accepts(handle, null, new PhoneAccountHandle())
                || PassionPhoneAccount.accepts(handle, null, null)
                || PassionPhoneAccount.accepts(null, null, null)) {
            throw new AssertionError();
        }

        PassionPhoneAccount.requireEnabled(manager, handle);

        if (manager.attempts != 1 || manager.enabledHandle != handle || !manager.current.enabled) {
            throw new AssertionError();
        }

        PassionPhoneAccount.requireEnabled(manager, handle);

        if (manager.attempts != 1) {
            throw new AssertionError();
        }

        manager.current.enabled = false;
        manager.refused = true;
        reject(() -> PassionPhoneAccount.requireEnabled(manager, handle));
        manager.refused = false;
        manager.denied = true;
        reject(() -> PassionPhoneAccount.requireEnabled(manager, handle));
        manager.denied = false;
        manager.vanished = true;
        reject(() -> PassionPhoneAccount.requireEnabled(manager, handle));
        int attempts = manager.attempts;
        reject(() -> PassionPhoneAccount.requireEnabled(manager, handle));

        if (manager.attempts != attempts) {
            throw new AssertionError();
        }

        oemDialer(manager);
        System.out.println(
                "PASS owned account enable, exact OEM default dialer, readback, refusal, permission"
                        + " denial, vanished and missing account; guest binding pending");
    }
}
