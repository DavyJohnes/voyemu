package com.qinggan.canbus.service;

import android.content.ContentProvider;
import android.content.Context;
import android.content.pm.Signature;
import android.content.pm.SigningInfo;
import android.os.Binder;

import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.HexFormat;

public class OemProviderTest {
    static void check(boolean b) {
        if (!b) {
            throw new AssertionError();
        }
    }

    static String sha256(byte[] bytes) throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
    }

    static void reject(VinIdentityProvider p) {
        int c = Binder.clears;

        try {
            p.call("getVin", null, null);
            throw new AssertionError("unauthorized caller accepted");
        } catch (SecurityException expected) {
            check(Binder.clears == c);
        }
    }

    public static void main(String[] a) throws Exception {
        Context ctx = new Context();

        ContentProvider.context = ctx;
        byte[] der = Files.readAllBytes(Path.of(a[0]));
        VinIdentityProvider p = new VinIdentityProvider(sha256(der));
        p.onCreate();
        Binder.uid = 10130;
        ctx.manager.signature = -1;
        Signature cert = new Signature(der);
        SigningInfo si = new SigningInfo();
        si.current = new Signature[] {cert};
        si.history = new Signature[] {cert};
        ctx.manager.signingInfo = si;
        com.qinggan.os.SystemProperties.value = "genuine";
        check("genuine".equals(p.call("getVin", null, null).getString("vin")));
        check(Binder.clears == 1 && Binder.restores == 1);
        VinIdentityProvider production = new VinIdentityProvider();
        production.onCreate();
        reject(production);
        VinIdentityProvider other = new VinIdentityProvider(sha256(new byte[] {1}));
        other.onCreate();
        reject(other);
        si.multiple = true;
        reject(p);
        si.multiple = false;
        si.history = new Signature[] {cert, cert};
        reject(p);
        si.history = null;
        reject(p);
        si.history = new Signature[] {new Signature(new byte[] {2})};
        reject(p);
        si.history = new Signature[] {cert};
        si.current = null;
        reject(p);
        si.current = new Signature[] {cert, cert};
        reject(p);
        si.current = new Signature[] {new Signature(new byte[] {1})};
        reject(p);
        si.current = new Signature[] {cert};
        ctx.manager.packages = new String[] {"com.baidu.naviauto", "other"};
        reject(p);
        ctx.manager.packages = new String[] {"com.baidu.naviauto"};
        Binder.uid = 1000;
        reject(p);
        Binder.uid = 10130;
        ctx.manager.signingInfo = null;
        reject(p);
        ctx.manager.signingInfo = si;
        ctx.manager.signature = 0;
        si.multiple = true;
        check("genuine".equals(p.call("getVin", null, null).getString("vin")));
        System.out.println(
                "PASS synthetic single signer accepted by its digest, OEM default and other digests"
                        + " rejected, ambiguous signers rejected and platform retained");
    }
}
