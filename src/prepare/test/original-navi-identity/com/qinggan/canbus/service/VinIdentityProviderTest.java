package com.qinggan.canbus.service;

import android.content.ContentProvider;
import android.content.Context;
import android.os.Binder;
import android.os.Bundle;

import com.qinggan.os.SystemProperties;

public class VinIdentityProviderTest {
    static void check(boolean x) {
        if (!x) {
            throw new AssertionError();
        }
    }

    static void rejected(VinIdentityProvider p) {
        int before = Binder.clears;
        boolean denied = false;

        try {
            p.call("getVin", null, null);
        } catch (SecurityException e) {
            denied = true;
        }

        check(denied && Binder.clears == before);
    }

    public static void main(String[] args) {
        Context c = new Context();

        ContentProvider.context = c;
        VinIdentityProvider p = new VinIdentityProvider();
        check(p.onCreate());
        Binder.uid = 0;
        rejected(p);
        Binder.uid = 2000;
        rejected(p);
        Binder.uid = 10131;
        rejected(p);
        Binder.uid = 10130;
        c.manager.packages = null;
        rejected(p);
        c.manager.packages = new String[] {"other"};
        rejected(p);
        c.manager.packages = new String[] {"com.baidu.naviauto", "other"};
        rejected(p);
        c.manager.packages = new String[] {"com.baidu.naviauto"};
        c.manager.signature = -1;
        rejected(p);
        c.manager.signature = 0;
        c.manager.missing = true;
        rejected(p);
        c.manager.missing = false;
        Binder.uid = 1000;
        c.manager.uid = 1000;
        rejected(p);
        Binder.uid = 10130;
        c.manager.uid = 10130;
        boolean invalid = false;

        try {
            p.call("readAny", "21", null);
        } catch (IllegalArgumentException e) {
            invalid = true;
        }

        check(invalid && Binder.clears == 0);
        invalid = false;

        try {
            p.call("getVin", "21", null);
        } catch (IllegalArgumentException e) {
            invalid = true;
        }

        check(invalid && Binder.clears == 0);
        Bundle extra = new Bundle();
        extra.putString("key", "21");
        invalid = false;

        try {
            p.call("getVin", null, extra);
        } catch (IllegalArgumentException e) {
            invalid = true;
        }

        check(invalid && Binder.clears == 0);
        invalid = false;

        try {
            p.insert(null, null);
        } catch (UnsupportedOperationException e) {
            invalid = true;
        }

        check(invalid);
        invalid = false;

        try {
            p.query(null, null, null, null, null);
        } catch (UnsupportedOperationException e) {
            invalid = true;
        }

        check(invalid);
        SystemProperties.value = "oem-genuine";
        check("oem-genuine".equals(p.call("getVin", null, null).getString("vin")));
        check(Binder.clears == 1 && Binder.restores == 1);
        SystemProperties.value = "";
        check(p.call("getVin", null, null).getString("vin") == null);
        SystemProperties.value = null;
        check(p.call("getVin", null, null).getString("vin") == null);
        SystemProperties.fail = true;
        check(p.call("getVin", null, null).getString("vin") == null);
        check(Binder.clears == 4 && Binder.restores == 4);
        System.out.println(
                "PASS exact caller and platform signature, rejected shared UID, fixed read,"
                        + " empty/error fail closed and restored identity");
    }
}
