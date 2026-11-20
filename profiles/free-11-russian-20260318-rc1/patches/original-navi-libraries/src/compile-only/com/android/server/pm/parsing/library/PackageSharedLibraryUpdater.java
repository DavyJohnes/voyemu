package com.android.server.pm.parsing.library;

import com.android.server.pm.parsing.pkg.ParsedPackage;

public abstract class PackageSharedLibraryUpdater {
    public PackageSharedLibraryUpdater() {}

    void prefixRequiredLibrary(ParsedPackage pkg, String library) {}

    public abstract void updatePackage(ParsedPackage pkg, boolean value);
}
