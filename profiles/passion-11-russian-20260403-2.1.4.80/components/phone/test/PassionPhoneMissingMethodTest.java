package com.voyemu.passion.phone;

import android.telecom.PhoneAccountHandle;
import android.telecom.TelecomManager;

public final class PassionPhoneMissingMethodTest {
    public static void main(String[] args) throws Exception {
        TelecomManager manager = new TelecomManager();

        try {
            PassionPhoneAccount.requireEnabled(manager, new PhoneAccountHandle());
            throw new AssertionError();
        } catch (NoSuchMethodException expected) {
        }

        try {
            PassionPhoneAccount.requireOemDialer(manager);
            throw new AssertionError();
        } catch (NoSuchMethodException expected) {
        }

        System.out.println(
                "PASS missing account-enable/default-dialer framework methods fail closed");
    }
}
