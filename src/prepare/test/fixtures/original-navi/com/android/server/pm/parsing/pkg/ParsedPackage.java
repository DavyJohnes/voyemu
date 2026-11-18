package com.android.server.pm.parsing.pkg;

import java.util.List;

public interface ParsedPackage {
    String getPackageName();

    List<String> getUsesLibraries();

    List<String> getUsesOptionalLibraries();

    ParsedPackage addUsesLibrary(int index, String name);

    ParsedPackage removeUsesOptionalLibrary(String name);
}
