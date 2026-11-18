package com.android.server.pm.parsing.library;

import com.android.server.pm.parsing.pkg.ParsedPackage;

import java.util.List;

/** Test double of the framework base: a library the package already uses is not added again. */
public abstract class PackageSharedLibraryUpdater {
    public PackageSharedLibraryUpdater() {}

    void prefixRequiredLibrary(ParsedPackage pkg, String library) {
        if (!uses(pkg.getUsesLibraries(), library)
                && !uses(pkg.getUsesOptionalLibraries(), library)) {
            pkg.addUsesLibrary(0, library);
        }
    }

    private static boolean uses(List<String> libraries, String library) {
        return libraries != null && libraries.contains(library);
    }

    public abstract void updatePackage(ParsedPackage pkg, boolean value);
}
