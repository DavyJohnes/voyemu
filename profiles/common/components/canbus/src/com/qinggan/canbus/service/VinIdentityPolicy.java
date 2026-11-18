package com.qinggan.canbus.service;

import android.content.pm.Signature;
import android.content.pm.SigningInfo;

import java.security.MessageDigest;
import java.util.Arrays;

final class VinIdentityPolicy {
    static final String OEM = "b2e962ed163aed0831d6d08ee41ca9406ae970032aa4df8a4c3b41d222b201d3";

    static boolean allow(
            int caller,
            int uid,
            String[] packages,
            boolean platform,
            SigningInfo info,
            String signer) {
        return caller >= 10000
                && caller == uid
                && packages != null
                && packages.length == 1
                && "com.baidu.naviauto".equals(packages[0])
                && (platform || oem(info, signer));
    }

    static boolean oem(SigningInfo info, String signer) {
        try {
            if (info == null || info.hasMultipleSigners()) {
                return false;
            }

            Signature[] c = info.getApkContentsSigners(), h = info.getSigningCertificateHistory();

            if (c == null
                    || h == null
                    || c.length != 1
                    || h.length != 1
                    || c[0] == null
                    || h[0] == null
                    || !Arrays.equals(c[0].toByteArray(), h[0].toByteArray())) {
                return false;
            }

            byte[] digest = MessageDigest.getInstance("SHA-256").digest(c[0].toByteArray());
            StringBuilder hex = new StringBuilder();

            for (byte b : digest) {
                hex.append(String.format("%02x", b & 255));
            }

            return signer.contentEquals(hex);
        } catch (java.security.NoSuchAlgorithmException | LinkageError e) {
            return false;
        }
    }
}
