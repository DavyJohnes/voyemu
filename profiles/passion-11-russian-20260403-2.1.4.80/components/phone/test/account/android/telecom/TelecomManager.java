package android.telecom;

public final class TelecomManager {
    public PhoneAccount current = new PhoneAccount();
    public PhoneAccountHandle enabledHandle;
    public int attempts;
    public boolean refused, denied, vanished;
    public String dialer = "com.android.dialer", requestedDialer;
    public boolean dialerRefused, dialerDenied, dialerUnchanged;
    public int dialerAttempts;

    public PhoneAccount getPhoneAccount(PhoneAccountHandle handle) {
        return current;
    }

    public void enablePhoneAccount(PhoneAccountHandle handle, boolean enable) {
        attempts++;
        enabledHandle = handle;

        if (denied) {
            throw new SecurityException("denied");
        }

        if (vanished) {
            current = null;
        } else if (!refused) {
            current.enabled = enable;
        }
    }

    public String getDefaultDialerPackage() {
        return dialer;
    }

    public boolean setDefaultDialer(String value) {
        dialerAttempts++;
        requestedDialer = value;

        if (dialerDenied) {
            throw new SecurityException("denied");
        }

        if (dialerRefused) {
            return false;
        }

        if (!dialerUnchanged) {
            dialer = value;
        }

        return true;
    }
}
