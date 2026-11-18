package com.qinggan.canbus.service;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.pm.PackageManager;
import android.content.pm.SigningInfo;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;

/** Fixed, caller-bound OEM identity read; never exposes general NVRAM access. */
public final class VinIdentityProvider extends ContentProvider {
    private final String signer;

    public VinIdentityProvider() {
        this(VinIdentityPolicy.OEM);
    }

    /**
     * Accepts a Navi signer whose single certificate has this SHA-256; production keeps the OEM
     * pin.
     */
    VinIdentityProvider(String signer) {
        this.signer = signer;
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public Bundle call(String method, String argument, Bundle extras) {
        if ((!"getVin".equals(method) && !"getVehicleType".equals(method))
                || argument != null
                || (extras != null && !extras.isEmpty())) {
            throw new IllegalArgumentException("fixed VIN read required");
        }

        final int caller = Binder.getCallingUid();
        PackageManager pm = getContext().getPackageManager();

        try {
            int uid = pm.getApplicationInfo("com.baidu.naviauto", 0).uid;
            boolean signed =
                    pm.checkSignatures(getContext().getPackageName(), "com.baidu.naviauto")
                            == PackageManager.SIGNATURE_MATCH;
            SigningInfo info = null;

            if (!signed) {
                info =
                        pm.getPackageInfo(
                                        "com.baidu.naviauto",
                                        PackageManager.GET_SIGNING_CERTIFICATES)
                                .signingInfo;
            }

            if (!VinIdentityPolicy.allow(
                    caller, uid, pm.getPackagesForUid(caller), signed, info, signer)) {
                throw new SecurityException("Navi caller required");
            }
        } catch (PackageManager.NameNotFoundException e) {
            throw new SecurityException("Navi caller unavailable");
        }

        final long identity = Binder.clearCallingIdentity();

        try {
            return readIdentity(method);
        } catch (ReflectiveOperationException | LinkageError e) {
            return new Bundle();
        } finally {
            Binder.restoreCallingIdentity(identity);
        }
    }

    private Bundle readIdentity(String method) throws ReflectiveOperationException {
        if ("getVehicleType".equals(method)) {
            ClassLoader loader =
                    new dalvik.system.PathClassLoader(
                            "/system/framework/QGVehicle.jar", getContext().getClassLoader());
            Class<?> c = Class.forName("com.qinggan.vehicle.VehicleConfigHelper", true, loader);

            if (c.getClassLoader() != loader) {
                return new Bundle();
            }

            Object type = c.getMethod("getVehicleTypeID").invoke(null);
            Bundle result = new Bundle();

            if (type instanceof Integer && ((Integer) type) == 134) {
                result.putInt("vehicle_type", (Integer) type);
            }

            return result;
        }

        Object value =
                Class.forName("com.qinggan.os.SystemProperties")
                        .getMethod("getVinCode")
                        .invoke(null);
        Bundle result = new Bundle();

        if (value instanceof String && !((String) value).isEmpty()) {
            result.putString("vin", (String) value);
        }

        return result;
    }

    @Override
    public Cursor query(
            Uri uri, String[] projection, String selection, String[] args, String order) {
        throw new UnsupportedOperationException();
    }

    @Override
    public String getType(Uri uri) {
        throw new UnsupportedOperationException();
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int delete(Uri uri, String selection, String[] args) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] args) {
        throw new UnsupportedOperationException();
    }
}
