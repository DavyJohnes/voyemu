package com.voyemu.passion.phone;

import android.telecom.PhoneAccount;
import android.telecom.PhoneAccountHandle;
import android.telecom.TelecomManager;

/** Enable only the registered local account; never select a system default account. */
final class PassionPhoneAccount {
    private static final String OEM_DIALER = "com.voyah.btphone";

    static boolean accepts(
            PhoneAccountHandle owned, PhoneAccountHandle manager, PhoneAccountHandle target) {
        return owned != null && owned.equals(target) && (manager == null || owned.equals(manager));
    }

    static void requireEnabled(TelecomManager manager, PhoneAccountHandle handle) throws Exception {
        PhoneAccount current = manager.getPhoneAccount(handle);

        if (current == null) {
            throw new IllegalStateException("PHONE_ACCOUNT_MISSING");
        }

        if (!current.isEnabled()) {
            TelecomManager.class
                    .getMethod("enablePhoneAccount", PhoneAccountHandle.class, boolean.class)
                    .invoke(manager, handle, true);
            current = manager.getPhoneAccount(handle);
        }

        if (current == null || !current.isEnabled()) {
            throw new IllegalStateException("PHONE_ACCOUNT_DISABLED");
        }
    }

    static void requireOemDialer(TelecomManager manager) throws Exception {
        if (OEM_DIALER.equals(manager.getDefaultDialerPackage())) {
            return;
        }

        Object changed =
                TelecomManager.class
                        .getMethod("setDefaultDialer", String.class)
                        .invoke(manager, OEM_DIALER);

        if (!Boolean.TRUE.equals(changed)
                || !OEM_DIALER.equals(manager.getDefaultDialerPackage())) {
            throw new IllegalStateException("PHONE_OEM_DIALER");
        }
    }
}
