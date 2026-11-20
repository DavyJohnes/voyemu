package com.android.server.pm.parsing.library;

import com.android.server.pm.parsing.pkg.ParsedPackage;

public final class QgLibraries extends PackageSharedLibraryUpdater {
    public static void apply(ParsedPackage p) {
        if (p != null && "com.baidu.naviauto".equals(p.getPackageName())) {
            new QgLibraries().updatePackage(p, false);
        }
    }

    @Override
    public void updatePackage(ParsedPackage p, boolean unused) {
        if (p == null || !"com.baidu.naviauto".equals(p.getPackageName())) {
            return;
        }

        prefixRequiredLibrary(p, "QGVehicle");
        prefixRequiredLibrary(p, "QGMOD");
        prefixRequiredLibrary(p, "QGAPI");
    }
}
