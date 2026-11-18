package com.voyemu.input;

import android.content.ContentProvider;
import android.os.Binder;
import android.os.Bundle;

/**
 * Synthetic Android transport checks exercise the unchanged production provider authority guard.
 */
public final class WheelBoundaryHostProbe {
    private static void check(boolean value) {
        if (!value) {
            throw new AssertionError();
        }
    }

    private static void denied(WheelControlProvider provider, int uid, boolean permission) {
        Binder.caller = uid;
        ContentProvider.context.permission = permission;
        boolean blocked = false;

        try {
            provider.call("bootstrap", null, new Bundle());
        } catch (SecurityException expected) {
            blocked = true;
        }

        check(blocked && WheelControl.calls == 0 && Binder.clears == 0 && Binder.restores == 0);
    }

    public static void main(String[] args) {
        WheelControlProvider provider = new WheelControlProvider();

        denied(provider, 10001, true);
        denied(provider, 1000, true);
        denied(provider, 2000, false);
        denied(provider, 0, false);
        Binder.caller = 2000;
        ContentProvider.context.permission = true;
        Bundle result = provider.call("bootstrap", null, new Bundle());
        check(result.getString("json").equals("{}") && WheelControl.calls == 1);
        check(Binder.clears == 1 && Binder.restores == 1);
        Binder.caller = 0;
        provider.call("input", null, new Bundle());
        check(WheelControl.calls == 2 && Binder.clears == 2 && Binder.restores == 2);
        provider.call("status", "bootstrap", new Bundle());
        check(WheelControl.calls == 3 && Binder.clears == 3 && Binder.restores == 3);
        Bundle invalid = provider.call("unknown", null, new Bundle());
        check(invalid.getString("error").equals("invalid-command"));
        check(WheelControl.calls == 3 && Binder.clears == 4 && Binder.restores == 4);
        boolean noFile = false;

        try {
            provider.insert(null, null);
        } catch (UnsupportedOperationException expected) {
            noFile = true;
        }

        check(noFile && WheelControl.calls == 3);
        System.out.println("PASS wheel provider caller boundary");
    }
}
