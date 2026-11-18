package android.telecom;

/** A framework without the two hidden setup methods must fail closed. */
public final class TelecomManager {
    public PhoneAccount getPhoneAccount(PhoneAccountHandle handle) {
        return new PhoneAccount();
    }

    public String getDefaultDialerPackage() {
        return "com.android.dialer";
    }
}
